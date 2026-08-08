@echo off
rem --------------------
rem make F/W image
rem %1 = MCU ('RL78G14', 'RL78G23, or 'RA2')
rem %2 = S-record file
rem %3 = F/W verion (Hex 4Byte (8 characters), no prefix)
rem %4 = size to divide
rem %5 = base file name of output
rem --------------------
if "%5"=="" goto error_make_fwimage
if not exist %2 goto error_make_fwimage
goto exec_make_fwimage

rem --------------------
rem  error
rem --------------------
:error_make_fwimage
echo invalid parameter
echo\ %%1 = MCU (RL78G14, RL78G23, RL78L23, RL78L23BankSwap, or RA2)
echo\         *Note: "RL78L23"         = RL78/L23 boot swap mode
echo\              : "RL78L23BankSwap" = RL78/L23 bank swap mode
echo\ %%2 = S-record file
echo\ %%3 = F/W verion (Hex 4Byte (8 characters), no prefix)
echo\ %%4 = size to divide
echo\ %%5 = base file name of output
goto end_make_fwimage

rem --------------------
rem  make F/W image
rem --------------------
:exec_make_fwimage

if "%~1"=="RL78L23" (
    rem -- RL78/L23 boot swap mode; same with RL78/G23 (address/range of BCL1 is same)
    set FWIMG_MCU=RL78G23
) else if "%~1"=="RL78L23BankSwap" (
    rem -- RL78/L23 bank swap mode
    set FWIMG_MCU=RL78L23
) else (
    set FWIMG_MCU=%1
)

rem --- delete temporary file ---
pushd %~dp0
del _tmp_fwimg.bin >NUL 2>&1
del _tmp_mot.mot   >NUL 2>&1
del %5_desc*.bin   >NUL 2>&1
popd

copy %2 %~dp0_tmp_mot.mot
pushd %~dp0

rem --- make F/W image ---
.\exe\fwimage_creation.exe %FWIMG_MCU% _tmp_mot.mot %3 _tmp_fwimg.bin
del _tmp_mot.mot >NUL 2>&1

rem --- add checksum ---
.\exe\fwimage_addchksum.exe _tmp_fwimg.bin %5
del _tmp_fwimg.bin >NUL 2>&1

rem --- split into file ---
.\exe\fwimage_divfile.exe %5 %4 -I
del %5 >NUL 2>&1

rem --- move F/W image to current ---
popd
move %~dp0%5_desc*.bin .\ >NUL 2>&1

:end_make_fwimage
@echo on
