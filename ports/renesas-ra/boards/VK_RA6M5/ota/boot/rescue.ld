/* Linker for VK_RA6M5 rescue stub — 32 KB at 0x00000000.
 *
 * The rescue stub MUST host its FCU code-flash write helpers in SRAM
 * (the FCU temporarily disables CF reads while erasing/programming, so
 * any code that touches CF during a flash op will fault).  We carve a
 * `.ramcode` section that lives in flash (LMA) but executes from RAM
 * (VMA); the reset handler copies it into RAM before calling main().
 */

MEMORY
{
    BOOT_FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 0x00008000   /* 32 KB */
    BOOT_RAM   (rwx): ORIGIN = 0x20000000, LENGTH = 0x00010000   /* 64 KB scratch (top of SRAM is for app) */
}

ENTRY(Reset_Handler)
__stack_top__ = ORIGIN(BOOT_RAM) + LENGTH(BOOT_RAM);

SECTIONS
{
    .vectors :
    {
        KEEP(*(.vectors))
    } > BOOT_FLASH

    .text :
    {
        *(.text*)
        *(.rodata*)
        *(.glue_7)
        *(.glue_7t)
        *(.gnu.linkonce.t.*)

        KEEP(*(.init))
        KEEP(*(.fini))

        __init_array_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;

        . = ALIGN(4);
    } > BOOT_FLASH

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > BOOT_FLASH
    __exidx_start = .;
    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > BOOT_FLASH
    __exidx_end = .;

    /* RAM-resident code: FCU CF-write helpers, anything that runs while
     * code-flash reads are stalled.  Stored in flash (LMA), copied to
     * RAM (VMA) at startup. */
    __ramcode_load__ = .;
    .ramcode : AT(__ramcode_load__)
    {
        __ramcode_start__ = .;
        *(.ramcode*)
        . = ALIGN(4);
        __ramcode_end__ = .;
    } > BOOT_RAM

    __etext = __ramcode_load__ + (__ramcode_end__ - __ramcode_start__);

    .data : AT(__etext)
    {
        __data_start__ = .;
        *(.data*)
        . = ALIGN(4);
        __data_end__ = .;
    } > BOOT_RAM

    .bss :
    {
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > BOOT_RAM

    /* Sanity: rescue stub image must fit in 32 KB. */
    ASSERT(__etext < ORIGIN(BOOT_FLASH) + LENGTH(BOOT_FLASH),
           "rescue stub image exceeds 32 KB")
}
