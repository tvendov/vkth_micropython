"""Native RX monitor + isolated VERIFY regression; tests remain in RAM."""
from pathlib import Path
import json
import time
import test_rx_tone_monitor_board_20260908 as native
import test_rx_tone_gui_small_20260908 as gui

WORK = Path(__file__).resolve().parent
RUN = Path((WORK/'gen-smooth-deployment-path.txt').read_text().strip())


def main():
    params = json.loads((RUN/'production-before.json').read_text())[0]
    params.update(m='AM',beon=1,a='OFF',v=0)
    prefix = native.HELPERS[:native.HELPERS.index('for k in range(120):')]
    prefix = prefix.replace('_saved_loader=s.load_params',
        's.save_params=lambda p:None\n_saved_loader=s.load_params')
    first = native.TEST.index('backend()\n')
    last = native.TEST.index("for name in ('am','usb','lsb','fm','cw'):")
    native_code = 'params='+repr(params)+'\n'+prefix+native.TEST[:first]+native.TEST[last:]
    for name,code in (('rx-native',native_code), ('rx-gui',gui.SETUP+gui.OPEN+gui.TEST)):
        compile(code,'<RAM '+name+'>','exec')
        (RUN/(name+'-ram.py')).write_text(code,encoding='utf-8')
        j = native.d.d.m.h.jopen()
        try:
            j.halt()
            binary = (RUN/'candidate/firmware.bin').read_bytes()
            assert bytes(j.memory_read8(0,len(binary))) == binary
            native.safe_boot(j,native.symbols(RUN/'candidate/firmware.elf')['boardctrl_run_boot_py'])
        finally:j.close()
        time.sleep(1)
        t = None
        try:
            t = native.d.af_connection()
            output = native.checked_execute(t,code,timeout=90)
            (RUN/(name+'-hil.log')).write_text(output,encoding='utf-8')
        finally:
            if t:native.d.d.m.h.serial_close(t)
            native.reset_after_test()


if __name__=='__main__':main()
