# Split-heap конфигурация за RA6M5

За да използвате и външната OSPI RAM, добавете в `main.c`:

```c
extern char _heap_internal_start, _heap_internal_end;
extern char _ospi_ram_start, _ospi_ram_end;

gc_init(&_heap_internal_start, &_heap_internal_end);
gc_init(&_ospi_ram_start, &_ospi_ram_end);
```

След компилация проверете с:

```python
import gc; print(gc.info())
```

трите полета `total`, `free` и `max_block` трябва да отчитат двата региона.
