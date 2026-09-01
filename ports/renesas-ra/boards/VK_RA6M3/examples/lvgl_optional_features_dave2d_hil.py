"""VK_RA6M3 HIL test for optional LVGL 9.4 features and Dave2D.

Upload this script together with ``lv_example_lottie_approve.json``.  The test
initializes the board display, registers a MicroPython-backed LVGL file-system
drive and checks observable output from every requested API.  It also verifies
that a Dave2D-supported frame produces at least one DRW interrupt.

The complex gradients use LVGL's software fallback by design.  The dedicated
plain-object probe is what proves that the Dave2D draw unit is active.
"""

import gc
import struct
import time

import lvgl as lv
from machine import LCD


LOTTIE_HOST_PATH = "/flash/lv_example_lottie_approve.json"
LOTTIE_DRIVE = "T"
LOTTIE_LVGL_PATH = LOTTIE_DRIVE + ":" + LOTTIE_HOST_PATH
LOTTIE_SIDE = 64

# Keep styles, callbacks, buffers and drivers reachable for as long as LVGL
# stores their C pointers.
KEEP = {}


def _init_display():
    """Initialize LVGL before asking it for a default display."""
    if (not lv.is_initialized()) or (lv.display_get_default() is None):
        from pRGB import RGB

        rgb = RGB()
        KEEP["rgb"] = rgb
        lcd = rgb.display
    else:
        lcd = LCD()

    display = lv.display_get_default()
    if display is None:
        raise RuntimeError("pRGB did not create an LVGL display")
    if not hasattr(lcd, "drw_stats"):
        raise RuntimeError("firmware has no LCD.drw_stats() Dave2D probe")
    return lcd, display


def _fs_open_cb(_driver, path, mode):
    if mode == lv.FS_MODE.RD:
        python_mode = "rb"
    elif mode == lv.FS_MODE.WR:
        python_mode = "wb"
    elif mode == (lv.FS_MODE.RD | lv.FS_MODE.WR):
        python_mode = "rb+"
    else:
        raise RuntimeError("unsupported LVGL file mode: %r" % (mode,))
    KEEP["fs_open_count"] = KEEP.get("fs_open_count", 0) + 1
    return {"file": open(path, python_mode), "path": path}


def _fs_close_cb(_driver, fs_file):
    fs_file.__cast__()["file"].close()
    return lv.FS_RES.OK


def _fs_read_cb(_driver, fs_file, buffer, bytes_to_read, bytes_read):
    data = fs_file.__cast__()["file"].read(bytes_to_read)
    buffer.__dereference__(bytes_to_read)[: len(data)] = data
    bytes_read.__dereference__(4)[:4] = struct.pack("<L", len(data))
    KEEP["fs_read_bytes"] = KEEP.get("fs_read_bytes", 0) + len(data)
    return lv.FS_RES.OK


def _fs_seek_cb(_driver, fs_file, position, whence):
    fs_file.__cast__()["file"].seek(position, whence)
    return lv.FS_RES.OK


def _fs_tell_cb(_driver, fs_file, position):
    value = fs_file.__cast__()["file"].tell()
    position.__dereference__(4)[:4] = struct.pack("<L", value)
    return lv.FS_RES.OK


def _register_fs(letter=LOTTIE_DRIVE):
    code = ord(letter)
    if lv.fs_is_ready(code):
        raise RuntimeError("LVGL drive %s is already registered" % letter)

    driver = lv.fs_drv_t()
    driver.init()
    driver.letter = code
    driver.open_cb = _fs_open_cb
    driver.close_cb = _fs_close_cb
    driver.read_cb = _fs_read_cb
    driver.seek_cb = _fs_seek_cb
    driver.tell_cb = _fs_tell_cb
    driver.cache_size = 0
    driver.register()
    KEEP["fs_driver"] = driver
    return driver


def _refresh(display, delay_ms=80):
    lv.refr_now(display)
    time.sleep_ms(delay_ms)


def _framebuffer_layout(lcd, display):
    framebuffer = memoryview(lcd)
    height = display.get_vertical_resolution()
    width = display.get_horizontal_resolution()
    if height <= 0 or len(framebuffer) % height:
        raise AssertionError("invalid LCD framebuffer dimensions")
    row_bytes = len(framebuffer) // height
    if row_bytes % 2:
        raise AssertionError("LCD framebuffer is not RGB565")
    stride = row_bytes // 2
    if stride < width:
        raise AssertionError("LCD framebuffer stride is smaller than display width")
    return framebuffer, stride


def _rgb565_pixel(framebuffer, stride, x, y):
    offset = (y * stride + x) * 2
    return framebuffer[offset] | (framebuffer[offset + 1] << 8)


def _region_signature(lcd, display, x1, y1, x2, y2):
    framebuffer, stride = _framebuffer_layout(lcd, display)
    checksum = 2166136261
    for y in range(y1, y2):
        for x in range(x1, x2):
            checksum ^= _rgb565_pixel(framebuffer, stride, x, y)
            checksum = (checksum * 16777619) & 0xFFFFFFFF
    return checksum


def _verify_gradient_pixels(lcd, display):
    framebuffer, stride = _framebuffer_layout(lcd, display)
    all_samples = []
    for x in (14, 132, 250):
        samples = tuple(
            _rgb565_pixel(framebuffer, stride, x + offset, 67)
            for offset in (12, 52, 92)
        )
        if samples[0] == samples[1] == samples[2]:
            raise AssertionError("complex gradient rendered as a flat color")
        all_samples.append(samples)
    print("PASS complex_gradients", all_samples)


def _clear_buffer(buffer):
    for index in range(len(buffer)):
        buffer[index] = 0


def _buffer_signature(buffer):
    nonzero = 0
    checksum = 2166136261
    for value in buffer:
        if value:
            nonzero += 1
        checksum ^= value
        checksum = (checksum * 16777619) & 0xFFFFFFFF
    return nonzero, checksum


def _force_lottie_middle_frame(lottie, display):
    animation = lottie.get_anim()
    if animation is None or animation.duration <= 0:
        raise AssertionError("Lottie source has no usable animation")
    animation.act_time = animation.duration // 2
    lv.anim_refr_now()
    _refresh(display, 40)


def _assert_lottie_pixels(source_name, render_buffer):
    signature = _buffer_signature(render_buffer)
    if signature[0] == 0:
        raise AssertionError("Lottie %s source rendered an empty buffer" % source_name)
    print("PASS lottie_%s" % source_name, signature)
    return signature


def _probe_dave2d(lcd, display, screen):
    """Draw only primitives accepted by the Dave2D evaluator."""
    screen.clean()
    screen.set_style_bg_color(lv.color_hex(0x101820), 0)
    _refresh(display)
    before = lcd.drw_stats()

    panel = lv.obj(screen)
    panel.set_size(440, 210)
    panel.center()
    panel.set_style_bg_color(lv.color_hex(0x176B87), 0)
    panel.set_style_border_color(lv.color_hex(0xF6C85F), 0)
    panel.set_style_border_width(6, 0)
    panel.set_style_radius(0, 0)

    label = lv.label(panel)
    label.set_text("Dave2D hardware probe")
    label.set_style_text_color(lv.color_hex(0xFFFFFF), 0)
    label.center()

    _refresh(display)
    after = lcd.drw_stats()
    if after[0] <= before[0]:
        raise AssertionError("Dave2D DRW interrupt counter did not advance")

    print("PASS dave2d_irq", before, after)
    screen.clean()
    _refresh(display)


def _add_gradient(screen, x, title, kind):
    gradient = lv.grad_dsc_t()
    gradient.init_stops(
        [lv.color_hex(0xFF3B30), lv.color_hex(0x34C759)],
        [lv.OPA.COVER, lv.OPA.TRANSP],
        None,
        2,
    )

    if kind == "linear":
        gradient.linear_init(0, 0, 105, 62, lv.GRAD_EXTEND.PAD)
    elif kind == "radial":
        gradient.radial_init(52, 31, 100, 31, lv.GRAD_EXTEND.PAD)
        gradient.radial_set_focal(35, 22, 5)
    elif kind == "conical":
        gradient.conical_init(lv.pct(50), lv.pct(50), 0, 270, lv.GRAD_EXTEND.PAD)
    else:
        raise ValueError("unknown gradient kind")

    style = lv.style_t()
    style.init()
    style.set_bg_grad(gradient)
    style.set_border_width(2)
    style.set_border_color(lv.color_hex(0xD9E2EC))
    style.set_radius(4)
    style.set_pad_all(0)

    panel = lv.obj(screen)
    panel.set_pos(x, 36)
    panel.set_size(105, 62)
    panel.add_style(style, 0)

    caption = lv.label(screen)
    caption.set_text(title)
    caption.set_pos(x, 102)

    KEEP.setdefault("gradients", []).append((gradient, style, panel, caption))


def _fixed_width_glyph(font, glyph, letter, next_letter):
    KEEP["glyph_calls"] = KEEP.get("glyph_calls", 0) + 1
    if not font.get_glyph_dsc_fmt_txt(glyph, letter, next_letter):
        return False
    glyph.adv_w = 20
    glyph.ofs_x = (glyph.adv_w - glyph.box_w) // 2
    return True


def _expect_matrix_dimension_error(matrix, value, case_name):
    try:
        matrix.m = value
    except ValueError:
        return
    raise AssertionError("matrix accepted invalid %s dimensions" % case_name)


def _assert_matrix_values(actual, expected):
    for row in range(3):
        for column in range(3):
            if abs(actual[row][column] - expected[row][column]) > 0.00001:
                raise AssertionError("matrix value round-trip mismatch")


def _add_matrix_and_font_examples(screen, lcd, display):
    transformed = lv.obj(screen)
    transformed.set_pos(370, 42)
    transformed.set_size(78, 48)
    transformed.set_style_bg_color(lv.color_hex(0xF6C85F), 0)
    transformed.set_style_border_width(0, 0)
    transformed.set_style_radius(2, 0)

    matrix = lv.matrix_t()
    _expect_matrix_dimension_error(
        matrix,
        [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0]],
        "outer",
    )
    _expect_matrix_dimension_error(
        matrix,
        [[1.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]],
        "inner",
    )
    expected = [
        [1.0, 0.25, 2.0],
        [-0.5, 1.0, 3.0],
        [0.0, 0.0, 1.0],
    ]
    matrix.m = expected
    _assert_matrix_values(matrix.m, expected)
    matrix.identity()
    transformed.set_transform(matrix)

    matrix_caption = lv.label(screen)
    matrix_caption.set_text("matrix")
    matrix_caption.set_pos(382, 102)

    normal = lv.label(screen)
    normal.set_text("Normal: 0123.Wabc")
    normal.set_pos(14, 134)
    normal.set_style_text_font(lv.font_montserrat_20, 0)

    mono_font = lv.font_t(lv.font_montserrat_20)
    mono_font.get_glyph_dsc = _fixed_width_glyph

    KEEP["glyph_calls"] = 0
    fixed = lv.label(screen)
    fixed.set_text("Fixed:  0123.Wabc")
    fixed.set_pos(14, 164)
    fixed.set_style_text_font(mono_font, 0)

    KEEP["matrix"] = matrix
    KEEP["mono_font"] = mono_font
    KEEP["matrix_objects"] = (transformed, matrix_caption, normal, fixed)

    _refresh(display)
    identity_signature = _region_signature(lcd, display, 340, 20, 480, 120)

    matrix.rotate(12.0)
    matrix.scale(1.10, 0.85)
    transformed.set_transform(matrix)
    _refresh(display)
    transformed_signature = _region_signature(lcd, display, 340, 20, 480, 120)
    if transformed_signature == identity_signature:
        raise AssertionError("matrix transform did not change framebuffer output")
    if KEEP["glyph_calls"] <= 0:
        raise AssertionError("custom glyph callback was not invoked")

    print("PASS matrix_array", expected)
    print("PASS matrix_render", identity_signature, transformed_signature)
    print("PASS glyph_callback", KEEP["glyph_calls"])


def _load_lottie_data(display, screen, json_data, render_buffer):
    _clear_buffer(render_buffer)
    lottie = lv.lottie(screen)
    lottie.set_src_data(json_data, len(json_data))
    lottie.set_buffer(LOTTIE_SIDE, LOTTIE_SIDE, render_buffer)
    lottie.set_pos(294, 190)
    _force_lottie_middle_frame(lottie, display)
    signature = _assert_lottie_pixels("data", render_buffer)
    lottie.delete()
    _refresh(display)
    gc.collect()
    return signature


def _load_lottie_file(display, screen, render_buffer):
    _clear_buffer(render_buffer)
    open_count = KEEP.get("fs_open_count", 0)
    read_bytes = KEEP.get("fs_read_bytes", 0)
    lottie = lv.lottie(screen)
    lottie.set_src_file(LOTTIE_LVGL_PATH)
    lottie.set_buffer(LOTTIE_SIDE, LOTTIE_SIDE, render_buffer)
    lottie.set_pos(382, 190)
    _force_lottie_middle_frame(lottie, display)
    if KEEP.get("fs_open_count", 0) <= open_count:
        raise AssertionError("Lottie file source did not open the LVGL path")
    if KEEP.get("fs_read_bytes", 0) <= read_bytes:
        raise AssertionError("Lottie file source did not read JSON bytes")
    signature = _assert_lottie_pixels("file", render_buffer)
    KEEP["lottie_file"] = lottie
    print("PASS lottie_file_io", LOTTIE_LVGL_PATH, KEEP["fs_read_bytes"] - read_bytes)
    return signature


def run():
    gc.collect()
    lcd, display = _init_display()
    screen = lv.screen_active()
    if screen is None:
        raise RuntimeError("LVGL has no active screen")

    _probe_dave2d(lcd, display, screen)
    _register_fs()

    with open(LOTTIE_HOST_PATH, "rb") as source:
        json_data = source.read()
    if not json_data:
        raise RuntimeError("Lottie JSON file is empty")

    render_buffer = bytearray(LOTTIE_SIDE * LOTTIE_SIDE * 4)
    KEEP["lottie_json"] = json_data
    KEEP["lottie_buffer"] = render_buffer

    screen.set_style_bg_color(lv.color_hex(0x101820), 0)
    screen.set_style_text_color(lv.color_hex(0xF5F7FA), 0)

    heading = lv.label(screen)
    heading.set_text("LVGL 9.4 / MicroPython / Dave2D")
    heading.set_pos(14, 8)

    _add_gradient(screen, 14, "linear", "linear")
    _add_gradient(screen, 132, "radial", "radial")
    _add_gradient(screen, 250, "conical", "conical")
    _add_matrix_and_font_examples(screen, lcd, display)
    _verify_gradient_pixels(lcd, display)

    lottie_caption = lv.label(screen)
    lottie_caption.set_text("Lottie: data + file validation")
    lottie_caption.set_pos(14, 224)

    data_signature = _load_lottie_data(display, screen, json_data, render_buffer)
    file_signature = _load_lottie_file(display, screen, render_buffer)
    KEEP["lottie_signatures"] = (data_signature, file_signature)
    _refresh(display)

    print("PASS heap_free", gc.mem_free())
    print("PASS final_drw_stats", lcd.drw_stats())
    return KEEP


APP = run()
