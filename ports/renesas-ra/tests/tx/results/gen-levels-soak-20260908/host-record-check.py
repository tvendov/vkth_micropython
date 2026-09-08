"""Read written SDR record separately from erased dataflash tail. No flash writes."""
from pathlib import Path
import json
import test_tx_generator_board_20260908 as old

root=Path(__file__).resolve().parent
run=root/'gen-levels-20260908-233725'
archive=Path((root/'rx-tone-monitor-deployment-path.txt').read_text().strip())
j=old.d.d.m.h.jopen()
try:
    j.halt()
    first=bytes(j.memory_read8(0x40100000,512))
    second=bytes(j.memory_read8(0x40100000,512))
    assert first[:4]==b'SDR1',first[:6]
    end=(6+int.from_bytes(first[4:6],'little')+3)&~3
    assert 8<=end<=512
    saved=(archive/'dataflash-record-before.bin').read_bytes()
    print('RECORD',end,'ARCHIVE_BYTES',len(saved),'WRITTEN_PREFIX_MATCH',first[:end]==saved[:end],
          'REPEATED_PREFIX_MATCH',first[:end]==second[:end],
          'FULL512_REPEAT_MATCH',first==second,
          'DIFF_OFFSETS',[k for k in range(512) if first[k]!=second[k]],flush=True)
    print('SAVED_PARAMS',json.loads(first[6:6+int.from_bytes(first[4:6],'little')]),flush=True)
    (run/'dataflash-readback-1.bin').write_bytes(first)
    (run/'dataflash-readback-2.bin').write_bytes(second)
finally:
    j.close()
    old.reset_after_test()
