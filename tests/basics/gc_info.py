import gc

if not hasattr(gc, "info"):
    print("SKIP")
    raise SystemExit

info = gc.info()
print(isinstance(info, tuple))
print(len(info) >= 7)
print(info[0] == info[1] + info[2])
