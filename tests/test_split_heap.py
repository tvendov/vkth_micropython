import gc

gc.collect()
info = gc.info()
assert info['max_block'] > 300 * 1024
