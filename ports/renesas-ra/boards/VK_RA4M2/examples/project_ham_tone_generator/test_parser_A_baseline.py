import gc
gc.collect()
m0 = gc.mem_free()
print("A:start mem=", m0)
print("A:ok")
gc.collect()
print("A:end mem=", gc.mem_free())
