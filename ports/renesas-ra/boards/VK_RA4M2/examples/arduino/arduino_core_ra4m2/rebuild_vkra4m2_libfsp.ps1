$ErrorActionPreference = "Stop"

$core = "C:\Users\teodor\AppData\Local\Arduino15\packages\arduino\hardware\vkra4m2\1.0.0"
$variant = Join-Path $core "variants\VK_RA4M2"
$fsp = Join-Path $core "extras\fsp"
$gccRoot = "C:\Users\teodor\AppData\Local\Arduino15\packages\arduino\tools\arm-none-eabi-gcc\7-2017q4\bin"
$gcc = Join-Path $gccRoot "arm-none-eabi-gcc.exe"
$ar = Join-Path $gccRoot "arm-none-eabi-ar.exe"
$build = "C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\boards\VK_RA4M2\examples\arduino\arduino_core_ra4m2\build_libfsp"

if (Test-Path $build) {
    Remove-Item -Recurse -Force $build
}
New-Item -ItemType Directory -Force -Path $build | Out-Null

$includes = @(
    "$fsp\ra\fsp\inc",
    "$fsp\ra\fsp\inc\api",
    "$fsp\ra\fsp\inc\instances",
    "$variant\includes\ra\arm\CMSIS_5\CMSIS\Core\Include",
    "$variant\includes\ra_gen",
    "$variant\includes\ra_cfg\fsp_cfg",
    "$variant\includes\ra_cfg\fsp_cfg\bsp",
    "$fsp\ra\fsp\src\bsp\cmsis\Device\RENESAS\Include",
    "$fsp\ra\fsp\src\bsp\mcu\all",
    "$fsp\ra\fsp\src\bsp\mcu\ra4m2"
)

$sources = @(
    "$fsp\ra\fsp\src\bsp\cmsis\Device\RENESAS\Source\startup.c",
    "$fsp\ra\fsp\src\bsp\cmsis\Device\RENESAS\Source\system.c",
    "$fsp\ra\fsp\src\bsp\mcu\all\bsp_clocks.c",
    "$fsp\ra\fsp\src\bsp\mcu\all\bsp_common.c",
    "$fsp\ra\fsp\src\bsp\mcu\all\bsp_delay.c",
    "$fsp\ra\fsp\src\bsp\mcu\all\bsp_group_irq.c",
    "$fsp\ra\fsp\src\bsp\mcu\all\bsp_guard.c",
    "$fsp\ra\fsp\src\bsp\mcu\all\bsp_io.c",
    "$fsp\ra\fsp\src\bsp\mcu\all\bsp_irq.c",
    "$fsp\ra\fsp\src\bsp\mcu\all\bsp_register_protection.c",
    "$fsp\ra\fsp\src\bsp\mcu\all\bsp_rom_registers.c",
    "$fsp\ra\fsp\src\bsp\mcu\all\bsp_sbrk.c",
    "$fsp\ra\fsp\src\bsp\mcu\all\bsp_security.c",
    "$fsp\ra\fsp\src\r_adc\r_adc.c",
    "$fsp\ra\fsp\src\r_agt\r_agt.c",
    "$fsp\ra\fsp\src\r_cgc\r_cgc.c",
    "$fsp\ra\fsp\src\r_dac\r_dac.c",
    "$fsp\ra\fsp\src\r_gpt\r_gpt.c",
    "$fsp\ra\fsp\src\r_ioport\r_ioport.c",
    "$fsp\ra\fsp\src\r_sci_uart\r_sci_uart.c",
    "$fsp\ra\fsp\src\r_sci_i2c\r_sci_i2c.c",
    "$fsp\ra\fsp\src\r_sci_spi\r_sci_spi.c",
    "$fsp\ra\fsp\src\r_iic_master\r_iic_master.c",
    "$fsp\ra\fsp\src\r_iic_slave\r_iic_slave.c",
    "$fsp\ra\fsp\src\r_icu\r_icu.c",
    "$fsp\ra\fsp\src\r_spi\r_spi.c",
    "$fsp\ra\fsp\src\r_can\r_can.c",
    "$fsp\ra\fsp\src\r_dtc\r_dtc.c",
    "$fsp\ra\fsp\src\r_lpm\r_lpm.c",
    "$fsp\ra\fsp\src\r_rtc\r_rtc.c",
    "$fsp\ra\fsp\src\r_wdt\r_wdt.c",
    "$fsp\ra\fsp\src\r_flash_hp\r_flash_hp.c"
)

$commonFlags = @(
    "-mcpu=cortex-m33",
    "-mthumb",
    "-mfloat-abi=hard",
    "-mfpu=fpv5-sp-d16",
    "-Os",
    "-ffunction-sections",
    "-fdata-sections",
    "-fsigned-char",
    "-fmessage-length=0",
    "-D_RA_CORE=CM33",
    "-D_RENESAS_RA_",
    "-include",
    "$variant\includes\ra_cfg\fsp_cfg\bsp\bsp_cfg.h",
    "-DFLASH_HP_CFG_CODE_FLASH_PROGRAMMING_ENABLE=1",
    "-DFLASH_HP_CFG_DATA_FLASH_PROGRAMMING_ENABLE=1"
)

$includeFlags = foreach ($inc in $includes) { "-I$inc" }
$objects = @()

foreach ($src in $sources) {
    if (-not (Test-Path $src)) {
        Write-Host "skip missing $src"
        continue
    }

    $obj = Join-Path $build ((Split-Path $src -Leaf) + ".o")
    & $gcc @commonFlags @includeFlags -c $src -o $obj
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $src" }
    $objects += $obj
}

$out = Join-Path $variant "libs\libfsp.a"
if (Test-Path $out) {
    Copy-Item -Force $out "$out.bak"
    Remove-Item -Force $out
}

& $ar rcs $out @objects
if ($LASTEXITCODE -ne 0) { throw "archive failed: $out" }

& (Join-Path $gccRoot "arm-none-eabi-nm.exe") -g --defined-only $out |
    Select-String -Pattern "R_GPT_|R_AGT_|R_IOPORT_|R_SCI_UART_|R_ADC_|R_DAC_" |
    Select-Object -First 30

Write-Host "rebuilt $out"
