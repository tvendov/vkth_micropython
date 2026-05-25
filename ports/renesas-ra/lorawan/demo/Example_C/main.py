import gc
gc.collect()
try:
    import class_c_demo
    class_c_demo.main()
except Exception as e:
    import sys
    print("BOOT FAIL:", e)
    sys.print_exception(e)
