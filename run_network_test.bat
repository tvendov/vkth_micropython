@echo off
echo RA6M5 Network Testing Suite
echo ========================

echo.
echo Checking Python installation...
python --version
if errorlevel 1 (
    echo ERROR: Python not found in PATH
    echo Please install Python or add it to PATH
    pause
    exit /b 1
)

echo.
echo Checking pyserial installation...
python -c "import serial; print('pyserial version:', serial.__version__)" 2>nul
if errorlevel 1 (
    echo pyserial not found, installing...
    pip install pyserial
    if errorlevel 1 (
        echo ERROR: Failed to install pyserial
        pause
        exit /b 1
    )
)

echo.
echo Starting network test upload and execution...
echo.
echo Make sure:
echo 1. VK-RA6M5 board is connected to COM4
echo 2. Ethernet cable is connected to the board
echo 3. DHCP server is available on your network
echo 4. No other terminal programs are using COM4
echo.
pause

python upload_and_test.py

echo.
echo Test completed. Check the output above for results.
pause
