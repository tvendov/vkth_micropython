"""Actual C AF queue/capture/plot functions with host peripheral stubs. No board I/O."""
import argparse
import os
from pathlib import Path
import re
import runpy
import subprocess
import tempfile

PORT = Path(__file__).resolve().parents[2]
extract = runpy.run_path(str(PORT / 'tests/rx/test_dac_chunks.py'))['function']

PRE = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ra_tone.h"
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define __DMB() ((void)0)
#define MICROPY_HW_ENABLE_IQ_ADC 1
#define RA_TX_ERROR_NONE 0
#define TX_WAIT_US 200
#define __DSB() ((void)0)
#define BSP_FEATURE_DMAC_MAX_CHANNEL 8
#define FSP_SUCCESS 0
#define FSP_ERR_IN_USE 1
#define FSP_ERR_TIMEOUT 2
#define FSP_INVALID_VECTOR -1
#define ELC_EVENT_ADC0_SCAN_END 41
#define TRANSFER_ADDR_MODE_INCREMENTED 1
#define TRANSFER_ADDR_MODE_FIXED 0
#define TRANSFER_SIZE_2_BYTE 1
#define TRANSFER_MODE_NORMAL 0
#define BSP_DELAY_UNITS_MICROSECONDS 0
typedef int fsp_err_t;
typedef void transfer_ctrl_t;
typedef struct {
    struct { unsigned dest_addr_mode,src_addr_mode,size,mode; } transfer_settings_word_b;
    const void *p_src; void *p_dest; size_t length;
} transfer_info_t;
typedef struct { unsigned channel; int irq,activation_source; } dmac_extended_cfg_t;
typedef struct { transfer_info_t *p_info; dmac_extended_cfg_t *p_extend; } transfer_cfg_t;
typedef struct { struct { unsigned DTE; } DMCNT_b; struct { unsigned ACT; } DMSTS_b;
                 struct { unsigned DMCRAL; } DMCRA_b; } reg_t;
typedef struct { reg_t *p_reg; } dmac_instance_ctrl_t;
static reg_t dma;
static struct { uint16_t ADDR[2]; } adc;
#define R_ADC0 (&adc)
static struct { uint32_t CYCCNT; } cycles;
#define DWT (&cycles)
static bool reserved[8];
static unsigned wait_calls, close_calls;
static bool ra_dmac_reserve(unsigned ch) { if(reserved[ch]) return false; reserved[ch]=true; return true; }
static void ra_dmac_release(unsigned ch) { CHECK(reserved[ch]); reserved[ch]=false; }
static void R_BSP_SoftwareDelay(unsigned n,int unit) { (void)n;(void)unit;wait_calls++; }
static int R_DMAC_Open(transfer_ctrl_t *ctrl,const transfer_cfg_t *cfg) {
    dmac_instance_ctrl_t *c=ctrl; c->p_reg=&dma; memset(&dma,0,sizeof dma);
    CHECK(cfg->p_extend->activation_source==ELC_EVENT_ADC0_SCAN_END);
    CHECK(cfg->p_info->p_src==&adc.ADDR[1]);
    dma.DMCRA_b.DMCRAL=cfg->p_info->length; return 0;
}
static int R_DMAC_Enable(transfer_ctrl_t *ctrl) { (void)ctrl; dma.DMCNT_b.DTE=1; return 0; }
static int R_DMAC_Disable(transfer_ctrl_t *ctrl) { (void)ctrl; dma.DMCNT_b.DTE=0; return 0; }
static int R_DMAC_Close(transfer_ctrl_t *ctrl) { (void)ctrl; CHECK(!dma.DMSTS_b.ACT); close_calls++; return 0; }
static int R_DMAC_Reset(transfer_ctrl_t *ctrl,void *src,void *dst,size_t n) {
    (void)ctrl; CHECK(src==&adc.ADDR[1] && dst && !dma.DMSTS_b.ACT && !dma.DMCNT_b.DTE);
    dma.DMCRA_b.DMCRAL=n; dma.DMCNT_b.DTE=1; return 0;
}
#define RA_IQ_SPEC_N 512
#define RA_IQ_AUDIO_RING_MASK 2047U
static uint32_t ra_disable_irq(void) { return 0; }
static void ra_enable_irq(uint32_t v) { (void)v; }
static bool rx_owned;
static bool ra_iq_adc_owns_adc(void) { return rx_owned; }
static uint16_t s_audio_ring[2048];
static uint32_t s_ring_head, s_ring_tail;
static struct { unsigned audio_underruns; } s_audio;
typedef struct {
    bool file_source, gen_source; int mode; uint32_t sample_rate_hz; uint16_t adc_mid;
} ra_tx_config_t;
static struct {
    ra_tx_config_t config;
    ra_tone_smooth_t generator;
    bool ready, adc_open;
    struct { bool running; int error; unsigned file_underruns; int af_error;
             bool af_enabled; unsigned af_frames,timer_clock_hz,timer_period; } status;
} tx;
static bool ra_tx_mode_is_ssb(int mode) { return mode == 3 || mode == 4; }
static void tx_unexpected_irq(void *p) { (void)p; CHECK(false); }
static uint16_t observed[1024];
static unsigned observed_n;
static void tx_cpu_sample(uint16_t raw, uint32_t began) {
    CHECK(began == cycles.CYCCNT); observed[observed_n++] = raw;
}
#define LCD_SCOPE_PIXELS 128U
#define LCD_SCOPE_FULL_SCALE 2048
#define LCD_WATERFALL_TOP_PAD 18
#define LCD_WATERFALL_SPEC_H 30
#define LCD_WATERFALL_GAP_PX 2
typedef struct { int32_t x1,y1,x2,y2; } lv_area_t;
'''

TEST = r'''
int main(void) {
    int16_t *a, *b;
    size_t n;
    rx_owned = true;
    CHECK(!ra_iq_adc_scope_workspace(&a, &b, &n));
    rx_owned = false;
    ra_iq_adc_scope_enable(1);
    CHECK(!ra_iq_adc_scope_workspace(&a, &b, &n));
    ra_iq_adc_scope_enable(0);
    CHECK(ra_iq_adc_scope_workspace(&a, &b, &n) && a != b && n == 512);
    CHECK(!ra_iq_adc_scope_workspace(NULL, &b, &n));
    tx.ready=tx.adc_open=tx.status.running=true;
    tx.config.adc_mid=2048; tx.status.timer_clock_hz=30000000; tx.status.timer_period=2500;
    CHECK(ra_tx_hw_scope_enable(true) && tx_scope.open && reserved[0]);
    const int16_t *mic;
    uint32_t rate;
    CHECK(!ra_tx_hw_scope_frame(&mic,&n,&rate));
    for(unsigned k=0;k<512;k++) tx_scope.half[0][k]=1000+k;
    dma.DMCNT_b.DTE=0; dma.DMCRA_b.DMCRAL=0;
    CHECK(ra_tx_hw_scope_frame(&mic,&n,&rate) && n==512 && rate==12000);
    CHECK(mic==a && mic[0]==-1048 && mic[511]==-537 && tx_scope.writing==1);
    for(unsigned k=0;k<512;k++) tx_scope.half[1][k]=2000+k;
    CHECK(mic[0]==-1048); /* other one-shot half cannot overwrite claimed frame */
    dma.DMCNT_b.DTE=0; dma.DMCRA_b.DMCRAL=0; dma.DMSTS_b.ACT=1;
    CHECK(!ra_tx_hw_scope_frame(&mic,&n,&rate));
    dma.DMSTS_b.ACT=0;
    CHECK(ra_tx_hw_scope_frame(&mic,&n,&rate) && mic==b && mic[0]==-48);
    CHECK(tx.status.af_frames==2);
    dma.DMSTS_b.ACT=1;
    CHECK(!ra_tx_hw_scope_enable(false) && tx_scope.open && reserved[0]);
    CHECK(wait_calls==TX_WAIT_US && close_calls==0 && tx.status.af_error==FSP_ERR_TIMEOUT);
    dma.DMSTS_b.ACT=0;
    CHECK(ra_tx_hw_scope_enable(false) && !tx_scope.open && !reserved[0]);
    memset(reserved,1,sizeof reserved);
    CHECK(!ra_tx_hw_scope_enable(true) && !tx_scope.open);
    CHECK(ra_tx_hw_scope_enable(false));
    memset(reserved,0,sizeof reserved);
    CHECK(ra_tx_hw_scope_enable(true) && tx.status.af_error==0);
    CHECK(ra_tx_hw_scope_enable(false));
    /* FILE callback must capture EXACTLY the selected AF before the modulator,
       at both the 24-kHz AM/FM rate and the 12-kHz SSB adapter rate. */
    for (unsigned dec = 1; dec <= 2; ++dec) {
        tx.config.file_source = true; tx.config.mode = dec == 1 ? 1 : 3;
        tx.ready = tx.status.running = true;
        tx.status.error = 0; tx.status.file_underruns = 0;
        s_ring_tail = 1990; s_ring_head = (s_ring_tail + 512 * dec) & 2047;
        observed_n = 0;
        for (unsigned k = 0; k < 512 * dec; ++k) {
            s_audio_ring[(1990 + k) & 2047] = 2048 + (int)(600 * sin(k * 0.17));
        }
        ra_iq_adc_scope_enable(1);
        for (unsigned k = 0; k < 512; ++k) { tx_file_callback(NULL); }
        const int16_t *frame;
        CHECK(ra_iq_adc_scope_frame(&frame, &n) && n == 512);
        CHECK(observed_n == 512 && s_ring_tail == s_ring_head);
        for (unsigned k = 0; k < n; ++k) { CHECK(frame[k] == (int)observed[k] - 2048); }
        CHECK(!ra_iq_adc_scope_frame(&frame, &n));
        CHECK(!tx.status.file_underruns);
        tx_file_callback(NULL);
        CHECK(observed[512] == 2048 && tx.status.file_underruns == 1);
        ra_iq_adc_scope_enable(0);
        tx_file_callback(NULL);
        CHECK(!ra_iq_adc_scope_frame(&frame, &n));
        CHECK(observed_n == 514); /* OFF stops display, NOT source/modulation */
    }
    /* GEN scope must see the same smoothed samples as the modulator, without
       an ADC or a FILE owner; disabling scope must not stop generation. */
    tx.config.file_source = false; tx.config.gen_source = true;
    tx.adc_open = false; observed_n = 0;
    CHECK(ra_tone_smooth_init(&tx.generator, 24000, 1, 10000, 2047, RA_TONE_SINE));
    CHECK(ra_tx_hw_scope_enable(true) && !tx_scope.open);
    for (unsigned k = 0; k < 512; ++k) { tx_gen_callback(NULL); }
    const int16_t *gen_frame;
    CHECK(ra_iq_adc_scope_frame(&gen_frame, &n) && n == 512);
    CHECK(observed_n == 512 && observed[0] == 2048);
    for (unsigned k = 0; k < n; ++k) { CHECK(gen_frame[k] == (int)observed[k] - 2048); }
    CHECK(ra_tx_hw_scope_enable(false));
    tx_gen_callback(NULL);
    CHECK(observed_n == 513 && !ra_iq_adc_scope_frame(&gen_frame, &n));
    uint16_t value = 99;
    CHECK(!ra_iq_adc_file_audio_next(&value, 3) && value == 99);
    CHECK(!ra_iq_adc_file_audio_next(NULL, 1));
    /* Fixed horizontal and vertical scales, independent of input amplitude. */
    lv_area_t area = {0,0,387,91};
    int16_t input[512], y[130];
    for (unsigned rate_i = 0; rate_i < 3; ++rate_i) {
        uint32_t rate = (uint32_t[]){12000,24000,44000}[rate_i];
        for (unsigned k = 0; k < 512; ++k) { input[k] = k; }
        y[0] = 12345; y[129] = 23456;
        CHECK(lcd_scope_prepare_rate(input, 512, &area, y + 1, rate));
        for (unsigned x = 0; x < 128; ++x) {
            CHECK(y[x + 1] == 50 - (int)((x * rate / 24000) * 31) / 2048);
        }
        CHECK(y[0] == 12345 && y[129] == 23456);
    }
    for (unsigned k = 0; k < 512; ++k) { input[k] = 1024; }
    CHECK(lcd_scope_prepare_rate(input, 512, &area, y, 24000));
    CHECK(y[0] == 35);
    for (unsigned k = 0; k < 512; ++k) { input[k] = 512; }
    CHECK(lcd_scope_prepare_rate(input, 512, &area, y, 24000));
    CHECK(y[0] == 43);
    CHECK(!lcd_scope_prepare_rate(input, 512, &area, y, 0));
    CHECK(!lcd_scope_prepare_rate(input, 255, &area, y, 24000));
    printf("PASS TX FILE/GEN AF actual C: %u checks; smoothed GEN, wrap, exact modulator/trace samples, underrun, OFF, scales\n", checks);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', required=True)
    args = parser.parse_args()
    rx = (PORT / 'ra/ra_iq_adc.c').read_text(encoding='utf-8')
    hw = (PORT / 'ra/ra_tx_hw.c').read_text(encoding='utf-8')
    header = (PORT / 'ra/ra_tx_hw.h').read_text(encoding='utf-8')
    lcd = (PORT / 'boards/VK_RA6M3/machine_lcd.c').read_text(encoding='utf-8')
    globals_ = '\n'.join(re.search(r'^static [^\n;]*\b' + name + r'\b[^;]*;', rx, re.M)[0]
                        for name in ('s_scope_audio', 's_scope_wr', 's_scope_half', 's_scope_ready', 's_scope_enable'))
    funcs = extract(header, 'ra_tx_software_source') + '\n'
    funcs += '\n'.join(extract(rx, name) for name in ('ra_iq_adc_scope_enable',
        'ra_iq_adc_scope_workspace', 'ra_iq_adc_scope_push', 'ra_iq_adc_scope_frame',
        'ra_iq_adc_file_audio_next'))
    funcs += '\n' + extract(hw, 'tx_file_callback')
    funcs += '\n' + extract(hw, 'tx_gen_callback')
    globals_ += '\n' + re.search(r'static struct \{[^}]*\} tx_scope;', hw)[0]
    funcs += '\n' + '\n'.join(extract(hw, name) for name in
                              ('tx_scope_close', 'ra_tx_hw_scope_enable', 'ra_tx_hw_scope_frame'))
    funcs += '\n' + extract(lcd, 'lcd_scope_prepare_rate')
    with tempfile.TemporaryDirectory(prefix='tx-af-') as tmp:
        c, exe = Path(tmp) / 'test.c', Path(tmp) / 'test.exe'
        c.write_text(PRE + globals_ + funcs + TEST, encoding='utf-8')
        env = dict(os.environ)
        env['PATH'] = str(Path(args.cc).parent) + os.pathsep + env.get('PATH', '')
        subprocess.run([args.cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I', str(PORT / 'ra'), str(c), str(PORT / 'ra/ra_tone.c'),
                        '-lm', '-o', str(exe)], check=True, env=env)
        subprocess.run([str(exe)], check=True, env=env)


if __name__ == '__main__':
    main()
