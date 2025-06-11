#!/usr/bin/env python3
"""
Working Server Test for VK-RA6M5
================================

Send commands one by one to avoid syntax errors and get a working server.
"""

import serial
import time
import requests
import threading

def create_working_server():
    """Create a working server by sending simple commands"""
    try:
        print("🔌 Connecting to VK-RA6M5 board on COM4...")
        ser = serial.Serial('COM4', 115200, timeout=3)
        print('✓ Connected to COM4')
        
        # Reset
        ser.write(b'\x03')
        time.sleep(1)
        ser.read_all()
        
        def send_cmd(cmd, wait_time=2.0):
            print(f"📤 {cmd}")
            ser.write(cmd.encode() + b'\r\n')
            time.sleep(wait_time)
            response = ser.read_all()
            if response:
                output = response.decode('utf-8', errors='ignore')
                lines = output.split('\n')
                for line in lines:
                    clean = line.strip()
                    if clean and not clean.startswith('>>>') and clean != cmd.strip() and clean != '...':
                        print(f"📥 {clean}")
                return output
            return None
        
        # Basic setup
        print("\n🔧 Setting up...")
        send_cmd('import network, socket, time')
        send_cmd('lan = network.LAN()')
        send_cmd('lan.active(True)')
        send_cmd('ip = lan.ifconfig()[0]')
        send_cmd('print(f"Board IP: {ip}")')
        
        # Create simple test data
        print("\n📁 Creating test data...")
        send_cmd('test_data = b"Hello from VK-RA6M5 board! This is a test file." * 20')  # ~1KB
        send_cmd('print(f"Test data size: {len(test_data)} bytes")')
        
        # Create server step by step
        print("\n🌐 Creating server...")
        send_cmd('server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)')
        send_cmd('server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)')
        send_cmd('server.bind(("0.0.0.0", 8080))')
        send_cmd('server.listen(1)')
        send_cmd('print(f"Server ready on {ip}:8080")')
        
        # Handle one request manually to test
        print("\n📡 Waiting for first request...")
        send_cmd('print("Waiting for connection...")')
        
        # Start monitoring in background
        def monitor_board():
            for i in range(60):  # Monitor for 1 minute
                if ser.in_waiting:
                    data = ser.read_all().decode('utf-8', errors='ignore')
                    if data.strip():
                        lines = data.strip().split('\n')
                        for line in lines:
                            clean = line.strip()
                            if clean and not clean.startswith('>>>'):
                                print(f"📥 {clean}")
                time.sleep(1)
        
        monitor_thread = threading.Thread(target=monitor_board, daemon=True)
        monitor_thread.start()
        
        # Now try to connect from outside
        print(f"\n🔽 Testing connection to server...")
        time.sleep(2)  # Give server time to start
        
        # Test connection
        try:
            print(f"📥 Attempting connection to http://192.168.1.141:8080/")
            response = requests.get('http://192.168.1.141:8080/', timeout=10)
            print(f"✅ Connection successful! Status: {response.status_code}")
            print(f"Response size: {len(response.content)} bytes")
            
            if response.content:
                content = response.content.decode('utf-8', errors='ignore')
                print(f"Content preview: {content[:100]}...")
                
                if "VK-RA6M5" in content or "Hello" in content:
                    print(f"✅ Board response verified!")
                    return True
                    
        except requests.exceptions.ConnectionError:
            print(f"❌ Connection refused - server not responding")
            
            # Try to manually handle the request on the board
            print(f"📤 Manually handling request...")
            send_cmd('client, addr = server.accept()')
            send_cmd('print(f"Connected from {addr[0]}")')
            send_cmd('request = client.recv(1024)')
            send_cmd('print(f"Request received: {len(request)} bytes")')
            
            # Send simple response
            send_cmd('response_headers = b"HTTP/1.1 200 OK\\r\\nContent-Type: text/plain\\r\\nContent-Length: " + str(len(test_data)).encode() + b"\\r\\nConnection: close\\r\\n\\r\\n"')
            send_cmd('client.send(response_headers)')
            send_cmd('client.send(test_data)')
            send_cmd('client.close()')
            send_cmd('print("Response sent")')
            
            # Try download again
            time.sleep(2)
            try:
                response = requests.get('http://192.168.1.141:8080/', timeout=10)
                print(f"✅ Second attempt successful! Status: {response.status_code}")
                return True
            except:
                print(f"❌ Second attempt also failed")
        
        except Exception as e:
            print(f"❌ Connection error: {e}")
        
        # Clean up
        send_cmd('server.close()')
        send_cmd('print("Server closed")')
        
        monitor_thread.join(timeout=5)
        ser.close()
        return False
        
    except Exception as e:
        print(f"❌ Error: {e}")
        try:
            ser.close()
        except:
            pass
        return False

def test_simple_loop_server():
    """Test a simple loop server that handles multiple requests"""
    try:
        print("🔌 Connecting for loop server test...")
        ser = serial.Serial('COM4', 115200, timeout=3)
        print('✓ Connected to COM4')
        
        # Reset
        ser.write(b'\x03')
        time.sleep(1)
        ser.read_all()
        
        def send_cmd(cmd, wait_time=2.0):
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
        
        # Setup
        send_cmd('import network, socket, time')
        send_cmd('lan = network.LAN()')
        send_cmd('lan.active(True)')
        send_cmd('ip = lan.ifconfig()[0]')
        send_cmd('print(f"IP: {ip}")')
        
        # Create test files
        send_cmd('data_1k = b"1KB test data from RA6M5! " * 40')  # ~1KB
        send_cmd('data_10k = b"10KB test data from RA6M5! " * 400')  # ~10KB
        send_cmd('print(f"1KB data: {len(data_1k)} bytes")')
        send_cmd('print(f"10KB data: {len(data_10k)} bytes")')
        
        # Simple server that handles requests one by one
        send_cmd('server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)')
        send_cmd('server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)')
        send_cmd('server.bind(("0.0.0.0", 8080))')
        send_cmd('server.listen(1)')
        send_cmd('print(f"Loop server ready on {ip}:8080")')
        
        # Handle requests in a simple loop
        send_cmd('request_count = 0')
        
        # Start monitoring
        def monitor_board():
            for i in range(90):  # Monitor for 1.5 minutes
                if ser.in_waiting:
                    data = ser.read_all().decode('utf-8', errors='ignore')
                    if data.strip():
                        lines = data.strip().split('\n')
                        for line in lines:
                            clean = line.strip()
                            if clean and not clean.startswith('>>>'):
                                print(f"📥 {clean}")
                time.sleep(1)
        
        monitor_thread = threading.Thread(target=monitor_board, daemon=True)
        monitor_thread.start()
        
        # Send the server loop commands
        print(f"\n🌐 Starting server loop...")
        
        # Simple server loop - handle one request at a time
        for request_num in range(3):  # Handle 3 requests
            print(f"\n📡 Setting up for request {request_num + 1}...")
            
            send_cmd('print(f"Waiting for request {request_count + 1}...")')
            send_cmd('client, addr = server.accept()')
            send_cmd('print(f"Request {request_count + 1} from {addr[0]}")')
            send_cmd('request = client.recv(1024).decode("utf-8")')
            send_cmd('path = request.split(" ")[1] if len(request.split(" ")) > 1 else "/"')
            send_cmd('print(f"Path: {path}")')
            
            # Handle different paths
            send_cmd('if path == "/":')
            send_cmd('    html = f"<html><body><h1>RA6M5 Test Server</h1><p>IP: {ip}</p><p><a href=\\"/test1k\\">1KB file</a></p><p><a href=\\"/test10k\\">10KB file</a></p></body></html>"')
            send_cmd('    response = f"HTTP/1.1 200 OK\\\\r\\\\nContent-Type: text/html\\\\r\\\\nContent-Length: {len(html)}\\\\r\\\\nConnection: close\\\\r\\\\n\\\\r\\\\n{html}"')
            send_cmd('    client.send(response.encode())')
            send_cmd('    print("Sent index page")')
            send_cmd('elif path == "/test1k":')
            send_cmd('    response = f"HTTP/1.1 200 OK\\\\r\\\\nContent-Type: application/octet-stream\\\\r\\\\nContent-Length: {len(data_1k)}\\\\r\\\\nConnection: close\\\\r\\\\n\\\\r\\\\n"')
            send_cmd('    client.send(response.encode())')
            send_cmd('    client.send(data_1k)')
            send_cmd('    print("Sent 1KB file")')
            send_cmd('elif path == "/test10k":')
            send_cmd('    response = f"HTTP/1.1 200 OK\\\\r\\\\nContent-Type: application/octet-stream\\\\r\\\\nContent-Length: {len(data_10k)}\\\\r\\\\nConnection: close\\\\r\\\\n\\\\r\\\\n"')
            send_cmd('    client.send(response.encode())')
            send_cmd('    client.send(data_10k)')
            send_cmd('    print("Sent 10KB file")')
            send_cmd('else:')
            send_cmd('    response = "HTTP/1.1 404 Not Found\\\\r\\\\nConnection: close\\\\r\\\\n\\\\r\\\\n404 Not Found"')
            send_cmd('    client.send(response.encode())')
            send_cmd('    print("Sent 404")')
            
            send_cmd('client.close()')
            send_cmd('request_count += 1')
            
            # Test this request
            time.sleep(2)
            
            if request_num == 0:
                # Test index page
                try:
                    print(f"\n🔽 Testing index page...")
                    response = requests.get('http://192.168.1.141:8080/', timeout=10)
                    if response.status_code == 200:
                        print(f"✅ Index page success: {len(response.content)} bytes")
                        if "RA6M5" in response.text:
                            print(f"✅ Content verified")
                    else:
                        print(f"❌ Index page failed: {response.status_code}")
                except Exception as e:
                    print(f"❌ Index page error: {e}")
                    
            elif request_num == 1:
                # Test 1KB file
                try:
                    print(f"\n🔽 Testing 1KB file...")
                    response = requests.get('http://192.168.1.141:8080/test1k', timeout=10)
                    if response.status_code == 200:
                        size = len(response.content)
                        print(f"✅ 1KB file success: {size} bytes")
                        if b"1KB test data" in response.content:
                            print(f"✅ 1KB content verified")
                    else:
                        print(f"❌ 1KB file failed: {response.status_code}")
                except Exception as e:
                    print(f"❌ 1KB file error: {e}")
                    
            elif request_num == 2:
                # Test 10KB file
                try:
                    print(f"\n🔽 Testing 10KB file...")
                    start_time = time.time()
                    response = requests.get('http://192.168.1.141:8080/test10k', timeout=15)
                    end_time = time.time()
                    
                    if response.status_code == 200:
                        size = len(response.content)
                        elapsed = end_time - start_time
                        speed = (size / elapsed) / 1024 if elapsed > 0 else 0  # KB/s
                        
                        print(f"✅ 10KB file success: {size} bytes in {elapsed:.2f}s ({speed:.1f}KB/s)")
                        if b"10KB test data" in response.content:
                            print(f"✅ 10KB content verified")
                            print(f"🎉 SUCCESS! Board can serve files via HTTP")
                            return True
                    else:
                        print(f"❌ 10KB file failed: {response.status_code}")
                except Exception as e:
                    print(f"❌ 10KB file error: {e}")
        
        # Clean up
        send_cmd('server.close()')
        send_cmd('print("Server stopped")')
        
        monitor_thread.join(timeout=5)
        ser.close()
        return False
        
    except Exception as e:
        print(f"❌ Error: {e}")
        try:
            ser.close()
        except:
            pass
        return False

def main():
    """Main test function"""
    print("🚀 VK-RA6M5 Working Server Test")
    print("Testing step-by-step server implementation")
    print("=" * 50)
    
    # Test 1: Basic server
    print("\n=== TEST 1: Basic Server ===")
    if create_working_server():
        print("✅ Basic server test successful!")
    else:
        print("❌ Basic server test failed")
    
    # Test 2: Loop server with file downloads
    print("\n=== TEST 2: Loop Server with File Downloads ===")
    if test_simple_loop_server():
        print("✅ Loop server test successful!")
        print("🎉 VK-RA6M5 board can serve files via HTTP!")
        print("📊 Ready to test larger files (100KB, 1MB, etc.)")
    else:
        print("❌ Loop server test failed")
    
    print(f"\n✅ Testing completed!")

if __name__ == "__main__":
    main()
