/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2016 Damien P. George
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

#include "fmode.h"
#include <py/mpconfig.h>
#include <fcntl.h>
#include <stdlib.h>

#ifndef STATIC
#define STATIC static
#endif

// Workaround for setting file translation mode: we must distinguish toolsets
// since mingw has no _set_fmode, and altering msvc's _fmode directly has no effect
STATIC int set_fmode_impl(int mode) {
    #ifndef _MSC_VER
    _fmode = mode;
    return 0;
    #else
    return _set_fmode(mode);
    #endif
}

void set_fmode_binary(void) {
    set_fmode_impl(O_BINARY);
}

void set_fmode_text(void) {
    set_fmode_impl(O_TEXT);
}
