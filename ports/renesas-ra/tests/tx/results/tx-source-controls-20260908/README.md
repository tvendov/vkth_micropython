# TX SOURCE/LOOP, TRANSCEIVER and warning status - 2026-09-08

Target: remembered J-Link 1120000058 / COM25 only.
Scope: Python app update, existing RadioOnly firmware unchanged; no tests in flash.
Tracked transcripts normalize UART CR/CRLF to LF; raw copies remain in the host archive.

## Evidence

- `deployment.json`: final source/MPY sizes and readback SHA256. Full 16-MiB
  QSPI archive before the initial source update; 64 previous files preserved.
  Later title/warning app updates retain the previous app under another name.
  Firmware/dataflash/code-tail preservation was verified during initial deployment;
  subsequent updates only performed two app file replacements and hash readback.
- `source-ui-hil.log`: final app, actual LVGL CLICKED callbacks dispatched in
  the GUI context. USB/LSB/AM/FM source selection, LOOP/ONCE/LOOP, BACK,
  independent source/mode, preserved owner, advancing AF and DSP, I/Q register
  samples and Si5351 CLK1 TX-x1 register readback at 579400 Hz.
  Test also asserts AF header, ADC label and cleared warning state after settling.
- `tx-warnings-readonly.log`: pre-fix, no reset or UI/source change. RX stopped,
  TX USB/R:USB, stale RX warning flags all true; six-second samples had zero
  ADC rails/clips/deadlines and FILE UND331 unchanged. AF advanced3202..3342.
- `production-rusb.log`: final normal application left TX USB579400, R:USB LOOP,
  AF advancing. The startup code also asserts TRANSCEIVER in RX/TX and no red
  warnings after settling. No reset after this final production start.

The diagnostic/test scripts were sent with echoed text plus SHA256 verification,
executed from RAM only. J-Link reset completed after the isolated HIL suite.
Original host scripts reside in
`C:/Users/teodor/Documents/Codex/2026-08-22/new-chat-2/`:
`test_tx_source_ui_board_20260908.py`, `inspect_tx_warnings_20260908.py`,
`deploy_tx_source_ui_20260908.py`, `finish_tx_source_title_20260908.py`,
`deploy_tx_warnings_20260908.py`.

## Host validation

Native Windows Python310 with `-E`, running each from `ports/renesas-ra/tests/tx`:

- `test_sdr_tx_source_ui.py`: PASS
- `test_sdr_tx_warnings.py`: PASS
- `test_sdr_fm_controls.py`: PASS
- `test_sdr_audio_controls.py`: PASS
- `test_sdr_tx_home.py`: PASS
- `test_sdr_tx_switch.py`: PASS

These execute extracted Python methods with doubles, not hardware emulation.
New warning coverage includes CW at host level; board suite covers AM/USB/LSB/FM.
Native firmware build: NOT RUN for this app-only change. MPY compile/readback: PASS.

## Limits

Physical touch/render appearance, analogue DAC waveform, RF frequency/power,
SSB rejection, AM depth and FM deviation were NOT measured.
Cumulative FILE underruns during transitions remain USB833/LSB1241/AM679/FM2402;
zero new events in the short settled windows is not proof of long-stream continuity.
Warning LEDs use per-poll growth; cumulative values remain in native status/BACKEND.
