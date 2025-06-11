@echo off
echo Launching TeraTerm for COM20...

REM Try different possible TeraTerm installation paths
if exist "C:\Program Files (x86)\teraterm\ttermpro.exe" (
    echo Found TeraTerm at Program Files (x86)
    "C:\Program Files (x86)\teraterm\ttermpro.exe" /C=20 /BAUD=115200
    goto :end
)

if exist "C:\Program Files\teraterm\ttermpro.exe" (
    echo Found TeraTerm at Program Files
    "C:\Program Files\teraterm\ttermpro.exe" /C=20 /BAUD=115200
    goto :end
)

if exist "C:\teraterm\ttermpro.exe" (
    echo Found TeraTerm at C:\teraterm
    "C:\teraterm\ttermpro.exe" /C=20 /BAUD=115200
    goto :end
)

REM Try to find it in PATH
ttermpro.exe /C=20 /BAUD=115200 2>nul
if %errorlevel% equ 0 goto :end

echo TeraTerm not found! Please install TeraTerm or check the path.
echo You can download it from: https://ttssh2.osdn.jp/
pause

:end
echo TeraTerm should be starting...
