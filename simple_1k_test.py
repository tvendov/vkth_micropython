#!/usr/bin/env python3
"""
Simple 1KB File Test for VK-RA6M5
=================================

Start with the absolute basics - create a 1KB file and serve it via HTTP.
Build up from there once this works.
"""

import serial
import time
import requests

def test_simple_server():
    """Test with minimal 1KB file"""
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
                    if clean and not clean.startswith('>>>') and clean != cmd.strip() and clean != '...':
                        print(f"📥 {clean}")
                return output
            return None
        
        # Basic setup
        print("\n🔧 Basic setup...")
        send_cmd('import network, socket, time, gc, os')
        send_cmd('lan = network.LAN()')
        send_cmd('lan.active(True)')
        send_cmd('ip = lan.ifconfig()[0]')
        send_cmd('print(f"IP: {ip}")')
        
        # Create tiny 1KB file
        print("\n📁 Creating 1KB test file...")
        send_cmd('data = b"Hello from RA6M5! " * 50')  # About 1KB
        send_cmd('with open("test1k.txt", "wb") as f: f.write(data)')
        send_cmd('size = len(data)')
        send_cmd('print(f"Created test1k.txt: {size} bytes")')
        
        # Very simple server - one request at a time
        print("\n🌐 Starting simple server...")
        
        # Send server code line by line to avoid syntax errors
        send_cmd('server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)')
        send_cmd('server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)')
        send_cmd('server.bind(("0.0.0.0", 8080))')
        send_cmd('server.listen(1)')
        send_cmd('print(f"Server listening on {ip}:8080")')
        
        # Handle one request manually
        send_cmd('print("Waiting for connection...")')
        send_cmd('client, addr = server.accept()')
        send_cmd('print(f"Connected from {addr[0]}")')
        
        # Read request
        send_cmd('request = client.recv(1024).decode("utf-8")')
        send_cmd('print(f"Request: {request.split()[0:2] if request else []}")')
        
        # Send simple response
        simple_response = '''
response = "HTTP/1.1 200 OK\\r\\nContent-Type: text/plain\\r\\nContent-Length: " + str(size) + "\\r\\nConnection: close\\r\\n\\r\\n"
client.send(response.encode())
client.send(data)
client.close()
print("Response sent")
'''
        send_cmd(simple_response, 3.0)
        
        send_cmd('server.close()')
        send_cmd('print("Server closed")')
        
        print(f"\n🎉 Simple server test completed!")
        print(f"🌐 Server was on: http://192.168.1.141:8080/")
        
        # Monitor for any additional output
        print(f"\n📡 Checking for server output...")
        for i in range(10):
            if ser.in_waiting:
                data = ser.read_all().decode('utf-8', errors='ignore')
                if data.strip():
                    lines = data.strip().split('\n')
                    for line in lines:
                        if line.strip():
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

def test_download():
    """Test downloading the 1KB file"""
    print(f"\n🔽 Testing download...")
    
    url = "http://192.168.1.141:8080/"
    
    try:
        print(f"📥 Downloading from {url}")
        response = requests.get(url, timeout=10)
        
        if response.status_code == 200:
            size = len(response.content)
            content = response.content.decode('utf-8', errors='ignore')
            
            print(f"✅ Download successful!")
            print(f"   Size: {size} bytes")
            print(f"   Content preview: {content[:50]}...")
            
            if "Hello from RA6M5" in content:
                print(f"✅ Content verified - board response correct!")
                return True, size
            else:
                print(f"❌ Unexpected content")
                return False, 0
        else:
            print(f"❌ HTTP error: {response.status_code}")
            return False, 0
            
    except requests.exceptions.ConnectionError:
        print(f"❌ Connection failed - server not responding")
        return False, 0
    except Exception as e:
        print(f"❌ Download error: {e}")
        return False, 0

def run_iterative_server():
    """Run server that handles multiple requests"""
    try:
        print("🔌 Connecting for iterative server test...")
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
        
        # Create test files of increasing sizes
        print("\n📁 Creating test files...")
        send_cmd('data1k = b"Test data 1KB! " * 64')  # ~1KB
        send_cmd('with open("test1k.bin", "wb") as f: f.write(data1k)')
        send_cmd('print(f"1KB file: {len(data1k)} bytes")')
        
        send_cmd('data10k = b"Test data 10KB! " * 640')  # ~10KB
        send_cmd('with open("test10k.bin", "wb") as f: f.write(data10k)')
        send_cmd('print(f"10KB file: {len(data10k)} bytes")')
        
        # Simple loop server
        print("\n🌐 Starting loop server...")
        
        loop_server = '''
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind(("0.0.0.0", 8080))
server.listen(1)
print(f"Loop server ready on {ip}:8080")

for i in range(5):
    try:
        client, addr = server.accept()
        print(f"Request {i+1} from {addr[0]}")
        
        request = client.recv(1024).decode('utf-8')
        path = request.split(' ')[1] if len(request.split(' ')) > 1 else '/'
        
        if 'test1k.bin' in path:
            response = f"HTTP/1.1 200 OK\\r\\nContent-Type: application/octet-stream\\r\\nContent-Length: {len(data1k)}\\r\\nConnection: close\\r\\n\\r\\n"
            client.send(response.encode())
            client.send(data1k)
            print(f"Sent 1KB file")
        elif 'test10k.bin' in path:
            response = f"HTTP/1.1 200 OK\\r\\nContent-Type: application/octet-stream\\r\\nContent-Length: {len(data10k)}\\r\\nConnection: close\\r\\n\\r\\n"
            client.send(response.encode())
            client.send(data10k)
            print(f"Sent 10KB file")
        else:
            html = f"<html><body><h1>RA6M5 Test</h1><p>IP: {ip}</p><p><a href='/test1k.bin'>1KB file</a></p><p><a href='/test10k.bin'>10KB file</a></p></body></html>"
            response = f"HTTP/1.1 200 OK\\r\\nContent-Type: text/html\\r\\nContent-Length: {len(html)}\\r\\nConnection: close\\r\\n\\r\\n{html}"
            client.send(response.encode())
            print("Sent index page")
        
        client.close()
        
    except Exception as e:
        print(f"Request error: {e}")

server.close()
print("Loop server stopped")
'''
        
        send_cmd(loop_server, 5.0)
        
        print(f"\n🎉 Loop server should be running!")
        print(f"🌐 Test URLs:")
        print(f"   http://192.168.1.141:8080/ (index)")
        print(f"   http://192.168.1.141:8080/test1k.bin (1KB)")
        print(f"   http://192.168.1.141:8080/test10k.bin (10KB)")
        
        # Monitor server activity
        print(f"\n📡 Monitoring server...")
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

def test_multiple_downloads():
    """Test downloading multiple file sizes"""
    print(f"\n🔽 Testing multiple downloads...")
    
    test_files = [
        ("http://192.168.1.141:8080/", "index page"),
        ("http://192.168.1.141:8080/test1k.bin", "1KB file"),
        ("http://192.168.1.141:8080/test10k.bin", "10KB file")
    ]
    
    results = []
    
    for url, description in test_files:
        print(f"\n📥 Testing {description}...")
        try:
            start_time = time.time()
            response = requests.get(url, timeout=15)
            end_time = time.time()
            
            if response.status_code == 200:
                size = len(response.content)
                elapsed = end_time - start_time
                speed = (size / elapsed) / 1024 if elapsed > 0 else 0  # KB/s
                
                print(f"✅ Success: {size} bytes in {elapsed:.2f}s ({speed:.1f}KB/s)")
                
                if description == "index page" and "RA6M5" in response.text:
                    print(f"✅ Index page content verified")
                elif description.endswith("file"):
                    print(f"✅ File download successful")
                
                results.append({
                    'description': description,
                    'size': size,
                    'speed': speed,
                    'success': True
                })
            else:
                print(f"❌ HTTP error: {response.status_code}")
                results.append({'description': description, 'success': False})
                
        except Exception as e:
            print(f"❌ Error: {e}")
            results.append({'description': description, 'success': False})
    
    return results

def main():
    """Main test function"""
    print("🚀 VK-RA6M5 Simple File Transfer Test")
    print("Starting with 1KB files and building up...")
    print("=" * 50)
    
    # Test 1: Simple single request server
    print("\n=== TEST 1: Single Request Server ===")
    if test_simple_server():
        print("✅ Basic server test completed")
        
        # Wait and test download
        time.sleep(2)
        success, size = test_download()
        if success:
            print(f"✅ 1KB download successful: {size} bytes")
        else:
            print("❌ 1KB download failed")
    
    # Test 2: Loop server with multiple files
    print("\n=== TEST 2: Multi-File Server ===")
    if run_iterative_server():
        print("✅ Loop server test completed")
        
        # Wait and test multiple downloads
        time.sleep(3)
        results = test_multiple_downloads()
        
        successful = [r for r in results if r.get('success', False)]
        
        print(f"\n📊 FINAL RESULTS:")
        print(f"Successful transfers: {len(successful)}/{len(results)}")
        
        for result in results:
            status = "✅" if result.get('success', False) else "❌"
            print(f"{status} {result['description']}")
            if result.get('success', False) and 'size' in result:
                print(f"    Size: {result['size']} bytes, Speed: {result['speed']:.1f}KB/s")
        
        if len(successful) >= 2:
            print(f"\n🎉 SUCCESS! Board can serve files via HTTP")
            max_size = max(r['size'] for r in successful if 'size' in r)
            print(f"Maximum file tested: {max_size} bytes")
            print(f"Ready to test larger files (100KB, 1MB, etc.)")
        else:
            print(f"\n⚠️  Partial success - some transfers failed")
    
    print(f"\n✅ Testing completed!")

if __name__ == "__main__":
    main()
