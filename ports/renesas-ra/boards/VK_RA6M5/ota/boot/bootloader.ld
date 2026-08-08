/*
 * Linker script for the VK_RA6M5 OTA bootloader.
 * Image lives in the first 64 KB of internal flash.  Reset-default
 * stack at the top of the first SRAM bank.
 */

MEMORY
{
    BOOT_FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 0x00010000   /* 64 KB */
    BOOT_RAM   (rwx): ORIGIN = 0x20000000, LENGTH = 0x00008000   /* 32 KB scratch */
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

        /* C++ static ctors/dtors — likely none for a bare-metal C boot
         * loader, but keep for safety. */
        __init_array_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;

        . = ALIGN(4);
    } > BOOT_FLASH

    /* Exception unwinding tables — empty for our code, but ARM ABI
     * insists on linker symbols.  Place in flash after .text. */
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

    __etext = .;        /* load address of .data in flash */

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

    /* Sanity: bootloader image must fit. */
    ASSERT(__etext < ORIGIN(BOOT_FLASH) + LENGTH(BOOT_FLASH),
           "bootloader image exceeds 64 KB")
}
