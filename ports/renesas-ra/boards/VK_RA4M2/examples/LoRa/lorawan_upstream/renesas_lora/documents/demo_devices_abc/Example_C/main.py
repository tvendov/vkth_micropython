import gc
import asyncio
gc.collect()
try:
    import class_c_demo
    asyncio.run(class_c_demo.main())
except Exception as e:
    print("BOOT FAIL:", e)
