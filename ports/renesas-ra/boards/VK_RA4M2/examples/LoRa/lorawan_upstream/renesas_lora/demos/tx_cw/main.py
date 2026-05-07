import gc
gc.collect()
try:
    import tx_cw_demo
except Exception as e:
    print('BOOT FAIL:', e)
