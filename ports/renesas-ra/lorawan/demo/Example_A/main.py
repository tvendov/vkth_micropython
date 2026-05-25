import gc
gc.collect()
try:
    import class_a_demo
    class_a_demo.main()
except Exception as e:
    import sys
    print("BOOT FAIL:", e)
    sys.print_exception(e)
