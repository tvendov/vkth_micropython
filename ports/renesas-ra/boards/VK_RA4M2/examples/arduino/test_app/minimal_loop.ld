MEMORY
{
  FLASH (rx)  : ORIGIN = 0x00010000, LENGTH = 0x00000100
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 0x00020000
}

ENTRY(Reset_Handler)

SECTIONS
{
  .vectors ORIGIN(FLASH) :
  {
    KEEP(*(.vectors))
  } > FLASH

  .text :
  {
    *(.text*)
  } > FLASH
}
