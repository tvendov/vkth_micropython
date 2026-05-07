import gc
gc.collect()
try:
    import spectrum_analyzer
except Exception as e:
    print('BOOT FAIL:', e)
