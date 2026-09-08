"""Host regression for the actual native capture gates + timer visibility prologue.

Extracted C code, peripheral/LVGL doubles. Does not execute FFT/render/IRQ paths.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


FIXTURE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#define MICROPY_HW_ENABLE_IQ_ADC 1
#define RA6M3 1
#define MICROPY_HW_ENABLE_TX 1
#define LCD_VIEW_SPECTRUM 0
#define LCD_VIEW_WATERFALL 1
#define LCD_VIEW_OFF 2
#define LCD_SCOPE_VIEW_TIME 0
#define LCD_SCOPE_VIEW_IQ 1
#define LCD_SCOPE_VIEW_OFF 2
typedef void lv_timer_t;
static void *s_lcd_spectrum_obj = (void *)1;
static bool valid = true, visible = true, home = true, tx_owned;
static bool s_lcd_spectrum_paused, s_lcd_spectrum_vfo_pending;
static unsigned s_lcd_spectrum_view, s_lcd_scope_view;
static bool fft, iq, scope, tx_scope;
static unsigned fft_starts;
static bool lv_obj_is_valid(void *obj) { return obj && valid; }
static bool lv_obj_is_visible(void *obj) { (void)obj; return visible; }
static void *lv_obj_get_screen(void *obj) { (void)obj; return (void *)1; }
static void *lv_screen_active(void) { return home ? (void *)1 : (void *)2; }
static bool ra_tx_hw_owns_resources(void) { return tx_owned; }
static void ra_iq_adc_spectrum_enable(bool on) {
    if (on && !fft) ++fft_starts;
    fft = on;
}
static void ra_iq_adc_constellation_enable(bool on) { iq = on; }
static void ra_iq_adc_scope_enable(bool on) { scope = on; }
static bool ra_tx_hw_scope_enable(bool on) { tx_scope = on; return true; }
'''

TEST = r'''
int main(void) {
    /* Same RX owner, HOME temporarily inactive. No Python pause setter or
     * source/epoch transition is available to repair the capture gate. */
    lcd_native_capture_apply(true);
    assert(fft && scope && !iq);
    home = false;
    lcd_lv_spectrum_timer_cb(NULL);
    assert(!fft && !scope && !iq);
    home = true;
    lcd_lv_spectrum_timer_cb(NULL);
    assert(fft && scope && !iq);
    unsigned starts = fft_starts;
    for (unsigned n=0; n<100; ++n) lcd_lv_spectrum_timer_cb(NULL);
    assert(fft_starts == starts); /* no accumulator restarts on steady ticks */

    visible = false;
    lcd_lv_spectrum_timer_cb(NULL);
    assert(!fft && !scope);
    visible = true;
    lcd_lv_spectrum_timer_cb(NULL);
    assert(fft && scope);

    /* A fresh IQADC clears its optional gates even if the native selector and
     * source identity did not change. One visible timer tick reconciles them. */
    fft = scope = iq = false;
    lcd_lv_spectrum_timer_cb(NULL);
    assert(fft && scope);

    for (unsigned left=0; left<3; ++left) {
        for (unsigned right=0; right<3; ++right) {
            for (unsigned pending=0; pending<2; ++pending) {
                s_lcd_spectrum_view = left;
                s_lcd_scope_view = right;
                s_lcd_spectrum_vfo_pending = pending;
                s_lcd_spectrum_paused = true;
                lcd_lv_spectrum_timer_cb(NULL);
                assert(!fft && !scope && !iq && !tx_scope);
                s_lcd_spectrum_paused = false;
                lcd_lv_spectrum_timer_cb(NULL);
                assert(fft == (left != LCD_VIEW_OFF || pending));
                assert(scope == (right == LCD_SCOPE_VIEW_TIME));
                assert(iq == (right == LCD_SCOPE_VIEW_IQ));
                assert(!tx_scope);
            }
        }
    }

    tx_owned = true;
    for (unsigned right=0; right<3; ++right) {
        s_lcd_scope_view = right;
        lcd_lv_spectrum_timer_cb(NULL);
        assert(!fft && !iq);
        assert(tx_scope == (right != LCD_SCOPE_VIEW_OFF));
        /* RX scope gating is intentionally not changed under a TX owner. */
    }
    tx_owned = false;
    s_lcd_spectrum_view = LCD_VIEW_SPECTRUM;
    s_lcd_scope_view = LCD_SCOPE_VIEW_TIME;
    lcd_lv_spectrum_timer_cb(NULL);
    assert(fft && scope && !tx_scope);
    valid = false;
    lcd_lv_spectrum_timer_cb(NULL);
    assert(!s_lcd_spectrum_obj && !fft && !scope && !iq);
    lcd_lv_spectrum_timer_cb(NULL);
    assert(!fft);
    puts("PASS actual LCD capture gates/visibility prologue: HOME resume, fresh RX, 18 view/pending combinations, steady ticks, TX ownership and invalid object");
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', required=True)
    args = parser.parse_args()
    port = Path(__file__).resolve().parents[2]
    source = (port/'boards/VK_RA6M3/machine_lcd.c').read_text()
    begin = source.index('static void lcd_native_capture_apply(bool attached) {')
    end = source.index('\n}\n', begin) + 3
    gates = source[begin:end]
    begin = source.index('static void lcd_lv_spectrum_timer_cb(lv_timer_t *timer) {')
    end = source.index('\n    #if defined(RA6M3) && MICROPY_HW_ENABLE_TX\n'
                       '    ra_tx_status_t tx_status;', begin)
    prologue = source[begin:end] + '\n}\n'
    env = dict(os.environ)
    env['PATH'] = str(Path(args.cc).parent) + os.pathsep + env.get('PATH', '')
    with tempfile.TemporaryDirectory(prefix='lcd-capture-resume-') as temporary:
        fixture = Path(temporary)/'test.c'
        binary = Path(temporary)/'test.exe'
        fixture.write_text(FIXTURE + gates + prologue + TEST)
        subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                        str(fixture), '-o', str(binary)], check=True, env=env)
        subprocess.run([str(binary)], check=True, env=env)
    print('Host only: full timer, FFT, framebuffer, IRQ timing and hardware recovery NOT VERIFIED.')


if __name__ == '__main__':
    main()
