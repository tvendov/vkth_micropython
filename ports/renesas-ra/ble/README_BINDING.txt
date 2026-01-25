MicroPython C module binding for RA BLE glue (FSP 4.4).
Files:
  - ra_ble_config.c, ra_ble.c, ra_ble.h
  - ra_ble_events.c, ra_ble_events.h
  - modra_ble.c  (MicroPython module: ra_ble)

Build notes (typical):
  1) Add modra_ble.c and ra_ble*.c to your ports/renesas-ra build (Makefile/manifest).
  2) Ensure you link Renesas BLE libraries (r_ble_compact) and RM_BLE_ABS config from QE.
  3) Call ra_ble.poll() regularly OR integrate ra_ble_process_events() into MICROPY_EVENT_POLL_HOOK.

Python usage:
  import ra_ble
  ra_ble.init()
  ra_ble.set_name("mp-ra4w1")
  ra_ble.adv_start(100, adv_data, None)
  while True:
      ra_ble.poll()
      evt = ra_ble.get_event()
      if evt:
          print(evt)

Notes (FSP 4.4 exact signatures):
- BD address is retrieved via Vendor Specific API: R_BLE_VS_GetBdAddr(), completion via BLE_VS_EVENT_GET_ADDR_COMP.
- Advertising start uses RM_BLE_ABS_StartLegacyAdvertising() with ble_abs_legacy_advertising_parameter_t (as in QE-generated app_main.c).
- Notification/Indication use st_ble_gatt_hdl_value_pair_t as required by r_ble_api.h.
