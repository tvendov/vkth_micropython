#!/usr/bin/env python3
"""
Get DHCP IP Address from VK-RA6M5 Board
======================================

Simple script to connect to the board and get the DHCP IP address.
"""

import serial
import time

def get_board_ip():
    """Connect to board and get DHCP IP address"""
    try:
        print("🔌 Connecting to VK-RA6M5 board on COM4...")
        ser = serial.Serial('COM4', 115200, timeout=3)
        print('✓ Connected to COM4')
        
        # Reset board to clean state
        ser.write(b'\x03\x04')  # Ctrl+C, Ctrl+D
        time.sleep(2)
        ser.read_all()  # Clear buffer
        
        def send_cmd(cmd, wait_time=2.0):
            """Send command and get response"""
            print(f"📤 {cmd}")
            ser.write(cmd.encode() + b'\r\n')
            time.sleep(wait_time)
            response = ser.read_all()
            if response:
                output = response.decode('utf-8', errors='ignore')
                print(f"📥 {output.strip()}")
                return output.strip()
            return None
        
        # Setup network and get IP
        print("\n🌐 Setting up network...")
        send_cmd('import network')
        send_cmd('import time')
        send_cmd('lan = network.LAN()')
        send_cmd('lan.active(True)')
        
        print("\n⏳ Getting DHCP configuration...")
        
        # Simple DHCP check
        dhcp_cmd = '''
config = lan.ifconfig()
if config[0] != '0.0.0.0':
    print(f"IP: {config[0]}")
    print(f"Gateway: {config[2]}")
    print(f"DNS: {config[3]}")
else:
    print("No IP address - waiting for DHCP...")
    for i in range(15):
        time.sleep(1)
        config = lan.ifconfig()
        if config[0] != '0.0.0.0':
            print(f"IP: {config[0]}")
            print(f"Gateway: {config[2]}")
            break
        if i % 3 == 0:
            print(f"Waiting... {15-i}s")
    else:
        print("DHCP failed")
'''
        
        result = send_cmd(dhcp_cmd, 20.0)
        
        # Extract IP from result
        if result and "IP:" in result:
            lines = result.split('\n')
            for line in lines:
                if line.strip().startswith('IP:'):
                    ip = line.split('IP:')[1].strip()
                    print(f"\n🎯 BOARD IP ADDRESS: {ip}")
                    return ip
        
        print("❌ Could not get IP address")
        return None
        
    except Exception as e:
        print(f"❌ Error: {e}")
        return None
    finally:
        try:
            ser.close()
        except:
            pass

if __name__ == "__main__":
    ip = get_board_ip()
    if ip:
        print(f"\n✅ SUCCESS: Board IP is {ip}")
        print(f"🌐 You can access the board at: http://{ip}:8080/")
    else:
        print("\n❌ FAILED: Could not get IP address")
        print("Check:")
        print("- Ethernet cable connected")
        print("- DHCP server available")
        print("- Board network configuration")
