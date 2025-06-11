import gc

info = gc.info()
assert isinstance(info, dict)
assert 'total' in info and 'max_block' in info
assert info['free'] + info['used'] == info['total']
