:mod:`gc` -- control the garbage collector
==========================================

.. module:: gc
   :synopsis: control the garbage collector

|see_cpython_module| :mod:`python:gc`.

Functions
---------

.. function:: enable()

   Enable automatic garbage collection.

.. function:: disable()

   Disable automatic garbage collection.  Heap memory can still be allocated,
   and garbage collection can still be initiated manually using :meth:`gc.collect`.

.. function:: collect()

   Run a garbage collection.

.. function:: mem_alloc()

   Return the number of bytes of heap RAM that are allocated by Python code.

   .. admonition:: Difference to CPython
      :class: attention

      This function is MicroPython extension.

.. function:: mem_free()

   Return the number of bytes of heap RAM that is available for Python
   code to allocate, or -1 if this amount is not known.

   .. admonition:: Difference to CPython
      :class: attention

      This function is MicroPython extension.

.. function:: threshold([amount])

   Set or query the additional GC allocation threshold. Normally, a collection
   is triggered only when a new allocation cannot be satisfied, i.e. on an
   out-of-memory (OOM) condition. If this function is called, in addition to
   OOM, a collection will be triggered each time after *amount* bytes have been
   allocated (in total, since the previous time such an amount of bytes
   have been allocated). *amount* is usually specified as less than the
   full heap size, with the intention to trigger a collection earlier than when the
   heap becomes exhausted, and in the hope that an early collection will prevent
   excessive memory fragmentation. This is a heuristic measure, the effect
   of which will vary from application to application, as well as
   the optimal value of the *amount* parameter.

   Calling the function without argument will return the current value of
   the threshold. A value of -1 means a disabled allocation threshold.

   .. admonition:: Difference to CPython
      :class: attention

      This function is a MicroPython extension. CPython has a similar
      function - ``set_threshold()``, but due to different GC
      implementations, its signature and semantics are different.

.. function:: info()

   Return a dictionary describing the current state of the heap with
   the following fields:

   ``total``
       Total heap size in bytes.

   ``used``
       Currently allocated bytes.

   ``free``
       Free bytes in the heap.

   ``max_free``
       Size of the largest contiguous free block.

   ``num_1block`` ``num_2block``
       Number of 1 and 2 block fragments.

   ``max_block``
       Maximum allocatable block size.

   When ``MICROPY_GC_SPLIT_HEAP_AUTO`` is enabled an additional
   ``max_new_split`` field is present.

   Example usage::

       >>> import gc
       >>> gc.info()
       {'total': 8504576, 'used': 102400, 'free': 8402176,
        'max_free': 5000000, 'num_1block': 5, 'num_2block': 2, 'max_block': 3000000}

   .. admonition:: Difference to CPython
      :class: attention

      This function is a MicroPython extension.

.. rubric:: GC mark-stack overflow

When the number of objects to mark during a collection exceeds
``MICROPY_ALLOC_GC_STACK_SIZE`` (default value is 64) the flag
``gc_stack_overflow`` is set and the collector performs a full heap
rescan instead of aborting the collection.

If this overflow occurs often consider increasing
``MICROPY_ALLOC_GC_STACK_SIZE`` in ``mpconfigport.h``::

    #define MICROPY_ALLOC_GC_STACK_SIZE 128
