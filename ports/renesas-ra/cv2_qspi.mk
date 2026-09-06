# Opt-in integration for the pinned SparkFun micropython-opencv tree.
ifeq ($(MICROPY_PY_CV2_QSPI),1)
ifneq ($(BOARD),VK_RA6M3)
$(error MICROPY_PY_CV2_QSPI requires VK_RA6M3)
endif
ifeq ($(SPARKFUN_CV2_DIR),)
$(error Pass USER_C_MODULES pointing to the directory containing micropython-opencv)
endif
LD_FILES = $(BUILD)/cv2_qspi.ld
CV2_BASELINE_BIN ?= build-CV2-QSPI-baseline/firmware.bin
CXXFLAGS += -fno-single-precision-constant -Wno-error=double-promotion -Wno-error=float-conversion
# SparkFun builds as C++11; the existing ThorVG sources need GCC's normal
# C++17 aggregate rules. Keep that language choice local to ThorVG.
$(LVGL_THORVG_OBJ): CXXFLAGS += -std=gnu++17
QSTR_RESPONSE_DRIVER = $(SPARKFUN_CV2_DIR)/platforms/ra6m3/qstr_response.py
SRC_C += lib/cmsis-dsp/Source/StatisticsFunctions/arm_min_q15.c
SRC_C += lib/cmsis-dsp/Source/StatisticsFunctions/arm_max_q15.c
# Use the full exception-enabled C++ runtime for this profile. The normal
# ThorVG-only runtime remains selected in builds without this option.
SRC_CXX := $(filter-out $(BOARD_DIR)/lvgl_thorvg_port.cpp,$(SRC_CXX))
HAL_SRC_C := $(filter-out $(HAL_DIR)/ra/fsp/src/bsp/mcu/all/bsp_sbrk.c,$(HAL_SRC_C))
# Full libstdc++ needs wide-character support absent from newlib-nano.
CV2_LIBC_FILE := "$(shell $(CC) $(CFLAGS) -print-file-name=libc.a)"
LIBS := $(subst $(LIBC_NANO_FILE_NAME),$(CV2_LIBC_FILE),$(LIBS))
# Newlib's reentrant formatting object also defines snprintf. Retain the
# normal MicroPython UART printf, but give its small snprintf private names.
$(BUILD)/shared/libc/printf.o: CFLAGS += -Dsnprintf=mp_cv_snprintf -Dvsnprintf=mp_cv_vsnprintf
$(BUILD)/$(SYSTEM_FILE): CFLAGS += -D__init_array_start=cv2_empty_init_start -D__init_array_end=cv2_empty_init_end
$(BUILD)/cv2_qspi.ld: $(BOARD_DIR)/vk_ra6m3.ld $(SPARKFUN_CV2_DIR)/platforms/ra6m3/make_linker.py
	$(Q)$(PYTHON) $(SPARKFUN_CV2_DIR)/platforms/ra6m3/make_linker.py $< $@
$(BUILD)/firmware.elf: $(BUILD)/cv2_qspi.ld $(SPARKFUN_CV2_BUILD)/lib/libopencv_core.a $(SPARKFUN_CV2_BUILD)/lib/libopencv_imgproc.a
$(BUILD)/firmware.bin: $(CV2_BASELINE_BIN) $(SPARKFUN_CV2_DIR)/platforms/ra6m3/package_image.py
endif
