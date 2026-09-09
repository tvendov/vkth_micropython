# TX AF/PWR deployment and bounded digital HIL — 2026-09-09

Target: remembered J-Link1120000058 / COM25. No target enumeration.
Candidate source commit: c2bbdc8ab; tag vk-ra6m3-tx-audio-band-drive-v1.
Firmware was built before that commit and reports 0b165cf1bd-dirty.
Candidate hashes are authoritative, not the version label alone.

Host runtime: `C:\Users\teodor\AppData\Local\Programs\Python\Python310\python.exe -E -u`.
Original host scripts ran from `C:\Users\teodor\Documents\Codex\2026-08-22\new-chat-2`.
They import the existing deployment/transport helpers there. Copies here are
execution evidence, not a standalone distribution of those helpers.

Commands (in that original directory):

```text
python.exe -E -u deploy_tx_filter_20260909.py
python.exe -E -u test_tx_filter_board_20260909.py
python.exe -E -u test_tx_filter_board_20260909.py --sources
python.exe -E -u test_tx_filter_board_20260909.py --production
```

- Full archive: canonical project `backups/tx-filter-1120000058-20260909-035048`.
- Internal 2MiB/QSPI16MiB/current dataflash record backed up before writes.
- Firmware and source/MPY readback PASS; all76 existing files preserved.
- Previous app retained under `sdr_single.pre-txfilter.*`; boot/main unchanged.
- QSPI code reserve4MiB and valid dataflash record unchanged during deployment.
- No test files installed. Test settings saves replaced in RAM only, then normal
  save restored for final production. RAM payload echo and SHA verified before execution.
- GEN AM/USB/LSB:15 HOME profiles,3 BACKEND selections,9 PWR cases PASS.
- MIC/FILE AM/USB/LSB:6 cases PASS; source/filter owner continuity and AF progress.
- Sources first attempt failed on a host-harness assumption: MIC label is None,
  not 'MIC'. Only that assertion was fixed; the failure log is retained.
- NORMAL J-Link reset after each isolated test including the failed attempt.
- No reset after final production start; UART closed.

Observed C body maxima: GEN AM1449/5000cycles, USB4509/10000, LSB4511/10000;
MIC AM1227/2728, FILE AM1234/5000, MIC SSB4309/10000, FILE SSB4729/10000.
No DSP deadline or clipping errors in successful test suites. R:USB transition
count85 underrun samples, steady delta0; other source cases steady delta0.
These are not an absolute ISR worst case or proof of loss-free UI transitions.

Final production: TX AM579900Hz, GEN SINE1kHz50%, PWR40%, depth50%, AF BASE.
AF frames and DSP samples advance; final status UND/CLIP/deadline0, free57552B.

Not tested: physical touch, calibrated MIC stimulus, analog filter response,
analog I/Q/RF power, native settings readback after a user RX save.
New PTT/KEY request is recorded in project next steps, not part of this upload.
