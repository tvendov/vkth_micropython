/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// qstrs specific to this port
// *FORMAT-OFF*

// Entries for sys.path
Q(/flash)
Q(/flash/lib)
Q(/sd)
Q(/sd/lib)

// For os.sep
Q(/)

// For machine.ADC vref selection.
Q(vref)
Q(avcc)
Q(external)
Q(internal)
Q(REF_AVCC)
Q(REF_EXTERNAL)
Q(REF_INTERNAL)

// For machine.ADC PGA support on RA6M3.
Q(pga_supported)
Q(pga)
Q(set_gain)
Q(gain)
Q(bypass)
Q(differential)
Q(PGA_OFF)
Q(PGA_BYPASS)
Q(PGA_SINGLE)
Q(PGA_DIFFERENTIAL)
Q(PGA_GAIN_2_000)
Q(PGA_GAIN_2_500)
Q(PGA_GAIN_2_667)
Q(PGA_GAIN_2_857)
Q(PGA_GAIN_3_077)
Q(PGA_GAIN_3_333)
Q(PGA_GAIN_3_636)
Q(PGA_GAIN_4_000)
Q(PGA_GAIN_4_444)
Q(PGA_GAIN_5_000)
Q(PGA_GAIN_5_714)
Q(PGA_GAIN_6_667)
Q(PGA_GAIN_8_000)
Q(PGA_GAIN_10_000)
Q(PGA_GAIN_13_333)
Q(PGA_DIFF_GAIN_1_500)
Q(PGA_DIFF_GAIN_2_333)
Q(PGA_DIFF_GAIN_4_000)
Q(PGA_DIFF_GAIN_5_667)

#if MICROPY_HW_ENABLE_USB
// for usb modes
Q(MSC+HID)
Q(VCP+MSC)
Q(VCP+HID)
#endif
