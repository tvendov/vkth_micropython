#!/usr/bin/env python3
"""
Check VK-RA6M5 Board Status
===========================

Simple script to check if the board is responding and what state it's in.
"""

import serial
import time

def check_board():
    """Check basic board connectivity and status"""
    try:
        print("🔌 Connecting to VK-RA6M5 board on COM4...")
        ser = serial.Serial('COM4', 115200, timeout=2)
        print('✓ Connected to COM4')
        
        # Send interrupt and newline
        print("📤 Sending interrupt signal...")
        ser.write(b'\x03')  # Ctrl+C
        time.sleep(1)
        ser.write(b'\r\n')
        time.sleep(1)
        
        # Read any response
        response = ser.read_all()
        if response:
            output = response.decode('utf-8', errors='ignore')
            print(f"📥 Initial response: {repr(output)}")
        
        # Try simple commands
        commands = [
            'print("Hello from board")',
            'import sys',
            'print(sys.version)',
            'import gc',
            'gc.collect()',
            'print(f"Free memory: {gc.mem_free()}")',
        ]
        
        for cmd in commands:
            print(f"\n📤 Sending: {cmd}")
            ser.write(cmd.encode() + b'\r\n')
            time.sleep(2)
            
            response = ser.read_all()
            if response:
                output = response.decode('utf-8', errors='ignore')
                print(f"📥 Response: {repr(output)}")
                
                # Clean up the response
                lines = output.split('\n')
                for line in lines:
                    clean = line.strip()
                    if clean and not clean.startswith('>>>') and clean != cmd:
                        print(f"✓ Board says: {clean}")
            else:
                print("❌ No response")
        
        # Try network import
        print(f"\n📤 Testing network import...")
        ser.write(b'import network\r\n')
        time.sleep(2)
        response = ser.read_all()
        if response:
            output = response.decode('utf-8', errors='ignore')
            print(f"📥 Network import response: {repr(output)}")
            if 'Traceback' in output or 'Error' in output:
                print("❌ Network module not available")
            else:
                print("✓ Network module imported successfully")
        
        # Check if LAN is available
        print(f"\n📤 Testing LAN interface...")
        ser.write(b'lan = network.LAN()\r\n')
        time.sleep(2)
        response = ser.read_all()
        if response:
            output = response.decode('utf-8', errors='ignore')
            print(f"📥 LAN creation response: {repr(output)}")
            if 'Traceback' in output or 'Error' in output:
                print("❌ LAN interface not available")
            else:
                print("✓ LAN interface created successfully")
                
                # Try to activate
                print(f"\n📤 Activating LAN...")
                ser.write(b'lan.active(True)\r\n')
                time.sleep(3)
                response = ser.read_all()
                if response:
                    output = response.decode('utf-8', errors='ignore')
                    print(f"📥 LAN activation response: {repr(output)}")
                
                # Check configuration
                print(f"\n📤 Checking network config...")
                ser.write(b'print(lan.ifconfig())\r\n')
                time.sleep(2)
                response = ser.read_all()
                if response:
                    output = response.decode('utf-8', errors='ignore')
                    print(f"📥 Network config: {repr(output)}")
                    
                    # Extract IP if present
                    if "'" in output and "." in output:
                        # Try to find IP pattern
                        import re
                        ip_pattern = r'\b(?:[0-9]{1,3}\.){3}[0-9]{1,3}\b'
                        ips = re.findall(ip_pattern, output)
                        if ips:
                            for ip in ips:
                                if ip != '0.0.0.0':
                                    print(f"🎯 FOUND IP ADDRESS: {ip}")
                                    return ip
                        print("📍 IP found but is 0.0.0.0 (no DHCP)")
                    else:
                        print("❌ No IP configuration found")
        
        ser.close()
        return None
        
    except Exception as e:
        print(f"❌ Error: {e}")
        return None

if __name__ == "__main__":
    print("🔍 Checking VK-RA6M5 board status...")
    ip = check_board()
    
    if ip:
        print(f"\n🎉 SUCCESS! Board IP: {ip}")
    else:
        print(f"\n❌ Could not get IP address")
        print("Possible issues:")
        print("- Ethernet cable not connected")
        print("- No DHCP server on network")
        print("- Board network not configured")
        print("- MicroPython network module not available")
