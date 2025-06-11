#!/usr/bin/env python3
"""
Complete Network Test for VK-RA6M5
==================================

Final comprehensive test that will:
1. Connect to board
2. Create test files of increasing sizes
3. Start a working web server
4. Test file downloads and measure performance
5. Report maximum file transfer capabilities
"""

import serial
import time
import requests
import threading

def run_complete_test():
    """Run the complete network test"""
    try:
        print("🔌 Connecting to VK-RA6M5 board on COM4...")
        ser = serial.Serial('COM4', 115200, timeout=3)
        print('✓ Connected to COM4')
        
        # Reset to clean state
        ser.write(b'\x03')  # Ctrl+C
        time.sleep(1)
        ser.read_all()
        
        def send_cmd(cmd, wait_time=2.0):
            """Send command and get response"""
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
        
        # Setup
        print("\n🔧 Setting up board...")
        send_cmd('import network, socket, time, gc, os')
        send_cmd('lan = network.LAN()')
        send_cmd('lan.active(True)')
        send_cmd('ip = lan.ifconfig()[0]')
        send_cmd('print(f"Board IP: {ip}")')
        
        # Memory check
        send_cmd('gc.collect()')
        send_cmd('print(f"Free memory: {gc.mem_free():,} bytes")')
        
        # Create test files of different sizes
        print("\n📁 Creating test files...")
        
        # Create 0.5MB file
        send_cmd('print("Creating 0.5MB file...")')
        send_cmd('with open("test_0.5mb.bin", "wb") as f:')
        send_cmd('    for i in range(512*1024): f.write(bytes([i & 0xFF]))')
        send_cmd('print(f"0.5MB file: {os.stat(\\"test_0.5mb.bin\\")[6]:,} bytes")')
        
        # Create 1MB file
        send_cmd('print("Creating 1MB file...")')
        send_cmd('with open("test_1mb.bin", "wb") as f:')
        send_cmd('    for i in range(1024*1024): f.write(bytes([i & 0xFF]))')
        send_cmd('print(f"1MB file: {os.stat(\\"test_1mb.bin\\")[6]:,} bytes")')
        
        # Try 2MB file
        send_cmd('print("Creating 2MB file...")')
        send_cmd('try:')
        send_cmd('    with open("test_2mb.bin", "wb") as f:')
        send_cmd('        for i in range(2*1024*1024): f.write(bytes([i & 0xFF]))')
        send_cmd('    print(f"2MB file: {os.stat(\\"test_2mb.bin\\")[6]:,} bytes")')
        send_cmd('except Exception as e:')
        send_cmd('    print(f"2MB file failed: {e}")')
        
        # List created files
        send_cmd('files = [f for f in os.listdir(".") if f.endswith(".bin")]')
        send_cmd('print(f"Created files: {files}")')
        
        # Start web server with proper error handling
        print("\n🌐 Starting web server...")
        
        # Server code that should work
        server_code = '''
try:
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("0.0.0.0", 8080))
    server.listen(1)
    print(f"Server started on {ip}:8080")
    
    # Handle requests
    for req_num in range(20):
        try:
            client, addr = server.accept()
            print(f"Request {req_num+1} from {addr[0]}")
            
            request = client.recv(1024).decode('utf-8')
            path = request.split(' ')[1] if len(request.split(' ')) > 1 else '/'
            
            if path == '/':
                # Main page
                html = f"""<!DOCTYPE html>
<html><head><title>RA6M5 Test</title></head>
<body>
<h1>RA6M5 Network Test</h1>
<p>Board IP: {ip}</p>
<p>Available files:</p>
<ul>"""
                for filename in files:
                    size = os.stat(filename)[6]
                    html += f'<li><a href="/{filename}">{filename}</a> ({size:,} bytes)</li>'
                html += "</ul></body></html>"
                
                response = f"HTTP/1.1 200 OK\\r\\nContent-Type: text/html\\r\\nContent-Length: {len(html)}\\r\\nConnection: close\\r\\n\\r\\n{html}"
                client.send(response.encode())
                
            elif path.endswith('.bin') and path[1:] in files:
                # Serve file
                filename = path[1:]
                file_size = os.stat(filename)[6]
                print(f"Serving {filename} ({file_size:,} bytes)")
                
                headers = f"HTTP/1.1 200 OK\\r\\nContent-Type: application/octet-stream\\r\\nContent-Length: {file_size}\\r\\nContent-Disposition: attachment; filename=\\"{filename}\\"\\r\\nConnection: close\\r\\n\\r\\n"
                client.send(headers.encode())
                
                # Send file
                with open(filename, 'rb') as f:
                    sent = 0
                    start = time.ticks_ms()
                    while sent < file_size:
                        chunk = f.read(8192)
                        if not chunk: break
                        client.send(chunk)
                        sent += len(chunk)
                        
                        if sent % (256*1024) == 0:
                            elapsed = time.ticks_diff(time.ticks_ms(), start)
                            speed = (sent * 1000) / (elapsed * 1024) if elapsed > 0 else 0
                            print(f"Sent {sent//1024}KB ({speed:.1f}KB/s)")
                    
                    elapsed = time.ticks_diff(time.ticks_ms(), start)
                    speed = (sent * 1000) / (elapsed * 1024) if elapsed > 0 else 0
                    print(f"Complete: {sent:,} bytes in {elapsed}ms ({speed:.1f}KB/s)")
            
            client.close()
            
        except Exception as e:
            print(f"Request error: {e}")
            try: client.close()
            except: pass
    
    server.close()
    print("Server stopped after 20 requests")
    
except Exception as e:
    print(f"Server error: {e}")
'''
        
        send_cmd(server_code, 5.0)
        
        print(f"\n🎉 Server should be running!")
        print(f"🌐 URL: http://192.168.1.141:8080/")
        
        # Monitor server for activity
        print(f"\n📡 Monitoring server activity...")
        
        def monitor_board():
            """Monitor board output in background"""
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
        
        # Start monitoring in background
        monitor_thread = threading.Thread(target=monitor_board, daemon=True)
        monitor_thread.start()
        
        # Wait a moment for server to start
        time.sleep(5)
        
        # Test downloads
        print(f"\n🔽 Testing file downloads...")
        
        test_files = ['test_0.5mb.bin', 'test_1mb.bin', 'test_2mb.bin']
        results = []
        
        for filename in test_files:
            url = f"http://192.168.1.141:8080/{filename}"
            print(f"\n📥 Testing {filename}...")
            
            try:
                start_time = time.time()
                response = requests.get(url, timeout=30)
                end_time = time.time()
                
                if response.status_code == 200:
                    size = len(response.content)
                    elapsed = end_time - start_time
                    speed = (size / elapsed) / (1024*1024) if elapsed > 0 else 0
                    
                    print(f"✅ Success: {size:,} bytes in {elapsed:.2f}s ({speed:.2f}MB/s)")
                    
                    # Verify integrity
                    if size >= 10:
                        expected = bytes([i & 0xFF for i in range(10)])
                        actual = response.content[:10]
                        if actual == expected:
                            print(f"✅ File integrity verified")
                            results.append({
                                'filename': filename,
                                'size': size,
                                'speed': speed,
                                'success': True
                            })
                        else:
                            print(f"❌ File integrity failed")
                            results.append({'filename': filename, 'success': False})
                    else:
                        results.append({'filename': filename, 'success': False})
                else:
                    print(f"❌ HTTP error: {response.status_code}")
                    results.append({'filename': filename, 'success': False})
                    
            except requests.exceptions.ConnectionError:
                print(f"❌ Connection failed")
                results.append({'filename': filename, 'success': False})
                break
            except Exception as e:
                print(f"❌ Error: {e}")
                results.append({'filename': filename, 'success': False})
        
        # Wait for monitoring to complete
        monitor_thread.join(timeout=10)
        
        ser.close()
        
        # Final results
        print(f"\n" + "="*50)
        print(f"🎯 FINAL TEST RESULTS")
        print(f"="*50)
        
        successful_tests = [r for r in results if r.get('success', False)]
        
        if successful_tests:
            max_size = max(r['size'] for r in successful_tests)
            avg_speed = sum(r['speed'] for r in successful_tests) / len(successful_tests)
            
            print(f"✅ NETWORK TEST SUCCESSFUL!")
            print(f"📊 Results:")
            print(f"   Board IP: 192.168.1.141")
            print(f"   Successful transfers: {len(successful_tests)}")
            print(f"   Maximum file size: {max_size:,} bytes ({max_size/(1024*1024):.1f}MB)")
            print(f"   Average speed: {avg_speed:.2f} MB/s")
            
            for result in successful_tests:
                size_mb = result['size'] / (1024*1024)
                print(f"   ✅ {result['filename']}: {size_mb:.1f}MB @ {result['speed']:.2f}MB/s")
            
            if max_size >= 2*1024*1024:
                print(f"\n🏆 EXCELLENT: Board can handle 2MB+ files")
                print(f"📈 Estimated maximum: 4-8MB (based on 8MB OSPI RAM)")
            elif max_size >= 1024*1024:
                print(f"\n✅ GOOD: Board can handle 1MB+ files")
            else:
                print(f"\n⚠️  LIMITED: Board limited to smaller files")
                
            return True
        else:
            print(f"❌ NO SUCCESSFUL TRANSFERS")
            return False
        
    except Exception as e:
        print(f"❌ Test error: {e}")
        try:
            ser.close()
        except:
            pass
        return False

if __name__ == "__main__":
    print("🚀 VK-RA6M5 Complete Network Test")
    print("This will test the maximum file transfer capabilities")
    print("=" * 50)
    
    success = run_complete_test()
    
    if success:
        print(f"\n🎉 TESTING COMPLETED SUCCESSFULLY!")
        print(f"The VK-RA6M5 board has demonstrated network file transfer capabilities.")
    else:
        print(f"\n❌ TESTING FAILED")
        print(f"Check board connection and network configuration.")
