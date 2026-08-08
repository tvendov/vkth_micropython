import gc
gc.collect()
m0 = gc.mem_free()
print("C:start mem=", m0)
x = 1  # this is a very long inline comment that goes on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on STOP
print("C:ok")
gc.collect()
print("C:end mem=", gc.mem_free())
