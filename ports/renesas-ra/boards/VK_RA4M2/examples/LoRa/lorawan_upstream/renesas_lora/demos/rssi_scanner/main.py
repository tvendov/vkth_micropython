import gc, asyncio
gc.collect()
try:
    import rssi_scanner
    asyncio.run(rssi_scanner.main())
except Exception as e:
    print('BOOT FAIL:', e)
