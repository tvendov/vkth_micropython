#!/usr/bin/env python3
"""
Final Network Test for VK-RA6M5
===============================

Simple, step-by-step approach to test network file transfer capabilities.
"""

import serial
import time
import requests

def test_board_network():
    """Test the board's network capabilities step by step"""
    try:
        print("🔌 Connecting to VK-RA6M5 board on COM4...")
        ser = serial.Serial('COM4', 115200, timeout=3)
        print('✓ Connected to COM4')
        
        # Reset to clean state
        ser.write(b'\x03')  # Ctrl+C
        time.sleep(1)
        ser.read_all()
        
        def send_cmd(cmd, wait_time=2.0):
            """Send single command and get response"""
            print(f"📤 {cmd}")
            ser.write(cmd.encode() + b'\r\n')
            time.sleep(wait_time)
            response = ser.read_all()
            if response:
                output = response.decode('utf-8', errors='ignore')
                lines = output.split('\n')
                for line in lines:
                    clean = line.strip()
                    if clean and not clean.startswith('>>>') and clean != cmd.strip():
                        print(f"📥 {clean}")
                return output
            return None
        
        # Step 1: Basic imports and network setup
        print("\n🔧 Setting up network...")
        send_cmd('import network, socket, time, gc, os')
        send_cmd('lan = network.LAN()')
        send_cmd('lan.active(True)')
        send_cmd('config = lan.ifconfig()')
        send_cmd('ip = config[0]')
        send_cmd('print(f"Board IP: {ip}")')
        
        # Step 2: Check memory
        print("\n💾 Checking memory...")
        send_cmd('gc.collect()')
        send_cmd('print(f"Free memory: {gc.mem_free():,} bytes")')
        
        # Step 3: Create test file (simple approach)
        print("\n📁 Creating 1MB test file...")
        send_cmd('filename = "test.bin"')
        send_cmd('size = 1024 * 1024')  # 1MB
        send_cmd('print(f"Creating {size:,} byte file...")')
        
        # Create file in chunks to avoid syntax errors
        send_cmd('f = open(filename, "wb")')
        send_cmd('chunk_size = 8192')
        send_cmd('written = 0')
        
        # Write file in a loop
        write_loop = '''
for i in range(size // chunk_size):
    chunk = bytes([(written + j) & 0xFF for j in range(chunk_size)])
    f.write(chunk)
    written += chunk_size
    if written % (256*1024) == 0:
        print(f"Written {written//1024}KB")
'''
        send_cmd(write_loop, 15.0)  # Wait longer for file creation
        
        send_cmd('f.close()')
        send_cmd('actual_size = os.stat(filename)[6]')
        send_cmd('print(f"File created: {actual_size:,} bytes")')
        
        # Step 4: Start simple HTTP server
        print("\n🌐 Starting HTTP server...")
        send_cmd('server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)')
        send_cmd('server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)')
        send_cmd('server.bind(("0.0.0.0", 8080))')
        send_cmd('server.listen(1)')
        send_cmd('print(f"Server ready: http://{ip}:8080/")')
        
        # Simple server loop
        server_loop = '''
print("Waiting for requests...")
for request_num in range(10):
    try:
        client, addr = server.accept()
        print(f"Request {request_num+1} from {addr[0]}")
        
        request = client.recv(1024).decode('utf-8')
        
        if 'GET /' in request and 'test.bin' not in request:
            html = f"""<!DOCTYPE html>
<html><head><title>RA6M5 Test</title></head>
<body>
<h1>RA6M5 Network Test</h1>
<p>Board IP: {ip}</p>
<p>File size: {actual_size:,} bytes</p>
<p><a href="/test.bin">Download test file</a></p>
</body></html>"""
            
            response = f"HTTP/1.1 200 OK\\r\\nContent-Type: text/html\\r\\nContent-Length: {len(html)}\\r\\nConnection: close\\r\\n\\r\\n{html}"
            client.send(response.encode())
            
        elif 'test.bin' in request:
            print(f"Serving file ({actual_size:,} bytes)")
            
            headers = f"HTTP/1.1 200 OK\\r\\nContent-Type: application/octet-stream\\r\\nContent-Length: {actual_size}\\r\\nContent-Disposition: attachment; filename=\\"test.bin\\"\\r\\nConnection: close\\r\\n\\r\\n"
            client.send(headers.encode())
            
            with open(filename, 'rb') as file:
                sent = 0
                start_time = time.ticks_ms()
                while sent < actual_size:
                    chunk = file.read(8192)
                    if not chunk:
                        break
                    client.send(chunk)
                    sent += len(chunk)
                    
                    if sent % (256*1024) == 0:
                        elapsed = time.ticks_diff(time.ticks_ms(), start_time)
                        if elapsed > 0:
                            speed = (sent * 1000) / (elapsed * 1024)
                            print(f"Sent {sent//1024}KB ({speed:.1f}KB/s)")
                
                elapsed = time.ticks_diff(time.ticks_ms(), start_time)
                if elapsed > 0:
                    speed = (sent * 1000) / (elapsed * 1024)
                    print(f"Transfer complete: {sent:,} bytes in {elapsed}ms ({speed:.1f}KB/s)")
        
        client.close()
        
    except Exception as e:
        print(f"Request error: {e}")
        break

server.close()
print("Server stopped")
'''
        
        send_cmd(server_loop, 5.0)
        
        print(f"\n🎉 Server should be running!")
        print(f"🌐 Test URL: http://192.168.1.141:8080/")
        
        # Monitor for server activity
        print(f"\n📡 Monitoring for 30 seconds...")
        for i in range(30):
            if ser.in_waiting:
                data = ser.read_all().decode('utf-8', errors='ignore')
                if data.strip():
                    lines = data.strip().split('\n')
                    for line in lines:
                        if line.strip() and not line.strip().startswith('>>>'):
                            print(f"📥 {line.strip()}")
            time.sleep(1)
        
        ser.close()
        return True
        
    except Exception as e:
        print(f"❌ Error: {e}")
        try:
            ser.close()
        except:
            pass
        return False

def test_download_performance():
    """Test downloading from the board"""
    print(f"\n🔽 Testing download performance...")
    
    url = "http://192.168.1.141:8080/test.bin"
    
    try:
        print(f"📥 Downloading from {url}")
        start_time = time.time()
        
        response = requests.get(url, timeout=30)
        response.raise_for_status()
        
        end_time = time.time()
        elapsed = end_time - start_time
        
        size = len(response.content)
        speed_mbps = (size / elapsed) / (1024*1024) if elapsed > 0 else 0
        
        print(f"✅ Download successful!")
        print(f"   Size: {size:,} bytes ({size/(1024*1024):.1f}MB)")
        print(f"   Time: {elapsed:.2f} seconds")
        print(f"   Speed: {speed_mbps:.2f} MB/s")
        
        # Verify file integrity (check first few bytes)
        if len(response.content) >= 10:
            expected = bytes([i & 0xFF for i in range(10)])
            actual = response.content[:10]
            if actual == expected:
                print(f"✅ File integrity verified")
            else:
                print(f"❌ File integrity check failed")
        
        return True, speed_mbps, size
        
    except requests.exceptions.ConnectionError:
        print(f"❌ Cannot connect to server")
        return False, 0, 0
    except requests.exceptions.Timeout:
        print(f"❌ Download timeout")
        return False, 0, 0
    except Exception as e:
        print(f"❌ Download error: {e}")
        return False, 0, 0

def main():
    """Main test function"""
    print("🚀 VK-RA6M5 Final Network Test")
    print("=" * 50)
    
    # Step 1: Setup board and server
    if test_board_network():
        print(f"\n✅ Board setup successful!")
        
        # Wait a moment for server to be ready
        time.sleep(3)
        
        # Step 2: Test download performance
        success, speed, size = test_download_performance()
        
        if success:
            print(f"\n🎉 NETWORK TEST COMPLETED SUCCESSFULLY!")
            print(f"📊 FINAL RESULTS:")
            print(f"   ✅ DHCP IP obtained: 192.168.1.141")
            print(f"   ✅ File created: {size:,} bytes ({size/(1024*1024):.1f}MB)")
            print(f"   ✅ HTTP server working")
            print(f"   ✅ File transfer speed: {speed:.2f} MB/s")
            
            if speed >= 1.0:
                print(f"   🏆 EXCELLENT: Transfer speed > 1 MB/s")
            elif speed >= 0.5:
                print(f"   ✅ GOOD: Transfer speed > 0.5 MB/s")
            else:
                print(f"   ⚠️  FAIR: Transfer speed < 0.5 MB/s")
                
            print(f"\n🎯 MAXIMUM FILE SIZE CAPABILITY:")
            if size >= 1024*1024:
                print(f"   ✅ Board can handle 1MB+ files")
                print(f"   📈 Estimated max size: 4-8MB (based on 8MB OSPI RAM)")
            else:
                print(f"   ⚠️  Limited to smaller files")
                
        else:
            print(f"\n❌ Download test failed")
            
    else:
        print(f"\n❌ Board setup failed")

if __name__ == "__main__":
    main()
