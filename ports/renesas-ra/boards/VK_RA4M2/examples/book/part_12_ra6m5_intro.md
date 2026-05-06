## Част XII: MicroPython за VK_RA6M5

> **Ключови термини:** **RA6M5** — Cortex-M33 @ 200 MHz с 512 KB SRAM, 2 MB Flash, hardware Ethernet MAC, USB HS, OctaSPI, SDHI. **OctaSPI RAM** — 8 MB външен SRAM, картиран в memory-mapped DOPI режим на адрес `0x68000000`. **Split GC heap** — MicroPython механизъм за регистриране на втора област за garbage collection (`gc_add()`). **DOPI** — Double Octa Peripheral Interface, 8 SDR/DDR линии за OSPI комуникация.

Тази част описва специфичното за платката `VK_RA6M5` (R7FA6M5BH) — по-голяма сестра на `VK_RA4M2`, с MicroPython port-а, който поддържа Ethernet (LAN), USB high-speed, SD карта и **8 MB OctaSPI RAM** активно интегрирана в garbage-collected heap-а на MicroPython.

Главите в тази част приемат, че читателят е минал поне Глави 0–1 от Част I (REPL, основни команди) и Глава 30 (управление на паметта в MicroPython). Кодовите примери се изпълняват по същия начин на VK_RA6M5 както и на VK_RA4M2 — REPL през USB CDC или UART9, `mpremote cp` за качване, `import machine` за периферии. Фокусът е върху възможностите, които VK_RA4M2 няма: голяма heap памет, мрежа, по-високо тактово ядро.

> **📌 Защо в книгата за VK_RA4M2:** API-то е едно и също. Знанията от глави 1–56 директно работят на VK_RA6M5; тази част добавя само това, което липсва на по-малкия борд. Ако вашата задача се вмества в 100 KB heap и не иска LAN — VK_RA4M2 е достатъчен. Ако трябват аудио буфери от мегабайти, HTTP клиент или симултанен достъп до много устройства — VK_RA6M5.
