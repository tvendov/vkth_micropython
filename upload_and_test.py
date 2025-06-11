"""
Upload and Test Script for RA6M5 Network Testing
===============================================

This script uploads the test files to the RA6M5 board and runs the network tests.
"""

import serial
import time
import os

def connect_to_board(port='COM4', baudrate=115200):
    """Connect to the MicroPython board"""
    try:
        ser = serial.Serial(port, baudrate, timeout=2)
        print(f"Connected to {port}")
        
        # Send Ctrl+C to interrupt any running program
        ser.write(b'\x03')
        time.sleep(0.5)
        
        # Send Enter to get prompt
        ser.write(b'\r\n')
        time.sleep(0.5)
        
        # Clear any pending data
        ser.read_all()
        
        return ser
    except Exception as e:
        print(f"Failed to connect to {port}: {e}")
        return None

def send_command(ser, command, wait_time=1):
    """Send a command to the board and read response"""
    print(f"Sending: {command}")
    ser.write(command.encode() + b'\r\n')
    time.sleep(wait_time)
    
    response = ser.read_all().decode('utf-8', errors='ignore')
    if response:
        print(f"Response: {response}")
    return response

def upload_file(ser, filename):
    """Upload a file to the board using paste mode"""
    if not os.path.exists(filename):
        print(f"File {filename} not found")
        return False
    
    print(f"Uploading {filename}...")
    
    with open(filename, 'r') as f:
        content = f.read()
    
    # Enter paste mode
    ser.write(b'\x05')  # Ctrl+E for paste mode
    time.sleep(0.5)
    
    # Send file creation command
    create_cmd = f"""
with open('{filename}', 'w') as f:
    f.write('''{content}''')
print('File {filename} uploaded successfully')
"""
    
    ser.write(create_cmd.encode())
    time.sleep(1)
    
    # Exit paste mode
    ser.write(b'\x04')  # Ctrl+D to execute
    time.sleep(2)
    
    # Read response
    response = ser.read_all().decode('utf-8', errors='ignore')
    print(f"Upload response: {response}")
    
    return "successfully" in response

def run_network_test(ser):
    """Run the network test sequence"""
    print("\n" + "="*50)
    print("Starting Network Test Sequence")
    print("="*50)
    
    # Step 1: Basic network setup test
    print("\n1. Running network setup test...")
    send_command(ser, "exec(open('network_setup.py').read())", 30)
    
    # Step 2: Memory test
    print("\n2. Running memory test...")
    send_command(ser, "exec(open('memory_test.py').read())", 20)
    
    # Step 3: Start web server
    print("\n3. Starting web server...")
    print("Note: Web server will run until Ctrl+C is pressed")
    send_command(ser, "exec(open('simple_web_server.py').read())", 5)
    
    print("\nWeb server should now be running!")
    print("Check the board's output for the IP address")
    print("Open http://[BOARD_IP]:8080/ in your browser to test file transfers")

def main():
    """Main function"""
    print("RA6M5 Network Test Upload and Execution")
    print("="*50)
    
    # Connect to board
    ser = connect_to_board()
    if not ser:
        return
    
    try:
        # Upload test files
        files_to_upload = [
            'network_setup.py',
            'memory_test.py', 
            'simple_web_server.py'
        ]
        
        print("\nUploading test files...")
        for filename in files_to_upload:
            if upload_file(ser, filename):
                print(f"✓ {filename} uploaded successfully")
            else:
                print(f"✗ Failed to upload {filename}")
                return
        
        # Run tests
        run_network_test(ser)
        
        # Keep connection open for monitoring
        print("\nMonitoring board output (Press Ctrl+C to exit)...")
        try:
            while True:
                if ser.in_waiting:
                    data = ser.read_all().decode('utf-8', errors='ignore')
                    if data:
                        print(data, end='')
                time.sleep(0.1)
        except KeyboardInterrupt:
            print("\nStopping monitoring...")
            
    finally:
        ser.close()
        print("Connection closed")

if __name__ == "__main__":
    # Check if pyserial is available
    try:
        import serial
    except ImportError:
        print("Error: pyserial not installed")
        print("Install with: pip install pyserial")
        exit(1)
    
    main()
