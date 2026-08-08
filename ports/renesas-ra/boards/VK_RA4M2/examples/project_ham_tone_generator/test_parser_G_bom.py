import gc
gc.collect()
m0 = gc.mem_free()
print("G:start mem=", m0)
# test comment
print("G:ok")
gc.collect()
print("G:end mem=", gc.mem_free())