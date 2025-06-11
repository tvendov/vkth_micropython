# Enable/disable extra modules
# VFS FAT FS support
MICROPY_VFS_FAT ?= 1

# Extra include so "py/mpconfig.h" is always reachable
CFLAGS_EXTRA += -I$(TOP)
INC          += -I$(TOP)

# -----------------------------------------------------------------
# Some generated sources use STATIC before mpconfig.h is included.
# Define it here so it’s always available.
CFLAGS_EXTRA += -DSTATIC=static