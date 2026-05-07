import gc, asyncio
gc.collect()
try:
    import tx_lora_pkt
    asyncio.run(tx_lora_pkt.main())
except Exception as e:
    print('BOOT FAIL:', e)
