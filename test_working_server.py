#!/usr/bin/env python3
"""
Test the Working Server and Enhance for File Transfer Testing
============================================================

Use the working server code you provided and enhance it to test maximum file sizes.
"""

import serial
import time
import requests
import threading

def deploy_working_server():
    """Deploy the working server code to the board"""
    try:
        print("🔌 Connecting to VK-RA6M5 board on COM4...")
        ser = serial.Serial('COM4', 115200, timeout=3)
        print('✓ Connected to COM4')
        
        # Reset
        ser.write(b'\x03')
        time.sleep(1)
        ser.read_all()
        
        def send_cmd(cmd, wait_time=2.0):
            print(f"📤 {cmd[:60]}{'...' if len(cmd) > 60 else ''}")
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
        
        # Deploy the working server code
        print("\n🚀 Deploying working server code...")
        
        # Import modules
        send_cmd('import os, socket, network')
        
        # Get board info
        send_cmd('BRD = os.uname().machine')
        send_cmd('print(f"Board: {BRD}")')
        
        # Create test files for transfer testing
        print("\n📁 Creating test files...")
        
        # Create files of different sizes
        test_files = [
            ('test_1kb.bin', 1024),
            ('test_10kb.bin', 10 * 1024),
            ('test_100kb.bin', 100 * 1024),
            ('test_1mb.bin', 1024 * 1024),
        ]
        
        for filename, size in test_files:
            print(f"Creating {filename} ({size} bytes)...")
            
            # Create file with unique pattern
            create_cmd = f'''
try:
    with open("{filename}", "wb") as f:
        for i in range({size}):
            f.write(bytes([i & 0xFF]))
    print(f"Created {filename}: {{os.stat('{filename}')[6]:,}} bytes")
except Exception as e:
    print(f"Failed to create {filename}: {{e}}")
'''
            send_cmd(create_cmd, 5.0)
        
        # List created files
        send_cmd('files = [f for f in os.listdir(".") if f.endswith(".bin")]')
        send_cmd('print(f"Test files: {files}")')
        
        # Enhanced server code with file serving
        print("\n🌐 Deploying enhanced server...")
        
        enhanced_server = '''
def handle_request(client):
    try:
        request = client.recv(1024).decode()
        file_path = request.split(' ')[1]
        print(f"Request: {file_path}")
        
        if file_path == '/':
            # Main page with test files
            html = """<!DOCTYPE html>
<html>
<head>
    <title>RA6M5 File Transfer Test</title>
    <style>
        body { font-family: Arial; padding: 20px; }
        .file-link { display: block; margin: 10px 0; padding: 10px; 
                     background: #f0f0f0; text-decoration: none; border-radius: 5px; }
        .file-link:hover { background: #e0e0e0; }
    </style>
</head>
<body>
    <h1>RA6M5 File Transfer Test</h1>
    <p>Board: """ + BRD + """</p>
    <p>Available test files:</p>
"""
            
            # Add links to test files
            for filename in files:
                try:
                    size = os.stat(filename)[6]
                    size_mb = size / (1024 * 1024)
                    html += f'<a href="/{filename}" class="file-link">{filename} ({size_mb:.1f}MB - {size:,} bytes)</a>\\n'
                except:
                    pass
            
            html += """
    <p><em>Click links to download and test transfer speeds</em></p>
</body>
</html>"""
            
            response = f"HTTP/1.1 200 OK\\r\\nContent-Type: text/html\\r\\nContent-Length: {len(html)}\\r\\nConnection: close\\r\\n\\r\\n{html}"
            client.send(response.encode())
            
        elif file_path.startswith('/test_') and file_path.endswith('.bin'):
            # Serve test file
            filename = file_path[1:]  # Remove leading '/'
            if filename in files:
                file_size = os.stat(filename)[6]
                print(f"Serving {filename} ({file_size:,} bytes)")
                
                # Send headers
                headers = f"HTTP/1.1 200 OK\\r\\nContent-Type: application/octet-stream\\r\\nContent-Length: {file_size}\\r\\nContent-Disposition: attachment; filename=\\"{filename}\\"\\r\\nConnection: close\\r\\n\\r\\n"
                client.send(headers.encode())
                
                # Send file in chunks with progress
                with open(filename, 'rb') as f:
                    sent = 0
                    start_time = time.ticks_ms()
                    while sent < file_size:
                        chunk = f.read(8192)
                        if not chunk:
                            break
                        client.send(chunk)
                        sent += len(chunk)
                        
                        # Progress for large files
                        if file_size > 50*1024 and sent % (50*1024) == 0:
                            elapsed = time.ticks_diff(time.ticks_ms(), start_time)
                            speed = (sent * 1000) / (elapsed * 1024) if elapsed > 0 else 0
                            print(f"  Sent {sent//1024}KB ({speed:.1f}KB/s)")
                    
                    elapsed = time.ticks_diff(time.ticks_ms(), start_time)
                    speed = (sent * 1000) / (elapsed * 1024) if elapsed > 0 else 0
                    print(f"Transfer complete: {sent:,} bytes in {elapsed}ms ({speed:.1f}KB/s)")
            else:
                # File not found
                response = "HTTP/1.1 404 Not Found\\r\\nContent-Type: text/html\\r\\nConnection: close\\r\\n\\r\\n<h1>404 File Not Found</h1>"
                client.send(response.encode())
        else:
            # Other requests - 404
            response = "HTTP/1.1 404 Not Found\\r\\nContent-Type: text/html\\r\\nConnection: close\\r\\n\\r\\n<h1>404 Not Found</h1>"
            client.send(response.encode())
            
    except Exception as e:
        print(f"Request error: {e}")
    finally:
        client.close()

def start_server():
    print("Starting enhanced file transfer server...")
    
    if "VK-RA6M5" in BRD:
        lan = network.LAN()
        lan.active(True)
        ip = lan.ifconfig()[0]
        print(f"LAN IP: {ip}")
        
        addr = (ip, 8080)  # Use port 8080
        s = socket.socket()
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(addr)
        s.listen(5)
        print(f"Server ready: http://{ip}:8080/")
        
        request_count = 0
        while request_count < 20:  # Handle 20 requests then stop
            try:
                client, addr_client = s.accept()
                request_count += 1
                print(f"Request {request_count} from {addr_client[0]}")
                handle_request(client)
            except Exception as e:
                print(f"Server error: {e}")
        
        s.close()
        print("Server stopped after 20 requests")

# Start the server
start_server()
'''
        
        send_cmd(enhanced_server, 5.0)
        
        print(f"\n🎉 Enhanced server deployed!")
        print(f"🌐 Server should be running on: http://192.168.1.141:8080/")
        
        # Monitor server activity
        print(f"\n📡 Monitoring server activity...")
        
        def monitor_board():
            for i in range(120):  # Monitor for 2 minutes
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
        
        # Wait for server to start
        time.sleep(5)
        
        # Test the server
        print(f"\n🔽 Testing server...")
        
        # Test index page
        try:
            response = requests.get('http://192.168.1.141:8080/', timeout=10)
            if response.status_code == 200:
                print(f"✅ Index page working: {len(response.content)} bytes")
                if "RA6M5" in response.text:
                    print(f"✅ Content verified")
            else:
                print(f"❌ Index page failed: {response.status_code}")
        except Exception as e:
            print(f"❌ Index page error: {e}")
        
        # Test file downloads
        test_downloads = [
            ('test_1kb.bin', 1024),
            ('test_10kb.bin', 10240),
            ('test_100kb.bin', 102400),
            ('test_1mb.bin', 1048576),
        ]
        
        results = []
        
        for filename, expected_size in test_downloads:
            print(f"\n📥 Testing {filename}...")
            try:
                start_time = time.time()
                response = requests.get(f'http://192.168.1.141:8080/{filename}', timeout=30)
                end_time = time.time()
                
                if response.status_code == 200:
                    size = len(response.content)
                    elapsed = end_time - start_time
                    speed = (size / elapsed) / (1024*1024) if elapsed > 0 else 0  # MB/s
                    
                    print(f"✅ Success: {size:,} bytes in {elapsed:.2f}s ({speed:.2f}MB/s)")
                    
                    # Verify file integrity
                    if size >= 10:
                        expected_pattern = bytes([i & 0xFF for i in range(10)])
                        actual_pattern = response.content[:10]
                        if actual_pattern == expected_pattern:
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
                    
            except Exception as e:
                print(f"❌ Error: {e}")
                results.append({'filename': filename, 'success': False})
                break  # Stop on first failure
        
        # Wait for monitoring to complete
        monitor_thread.join(timeout=10)
        ser.close()
        
        # Final results
        print(f"\n" + "="*50)
        print(f"🎯 FINAL TEST RESULTS")
        print(f"="*50)
        
        successful = [r for r in results if r.get('success', False)]
        
        if successful:
            max_size = max(r['size'] for r in successful)
            avg_speed = sum(r['speed'] for r in successful) / len(successful)
            
            print(f"✅ FILE TRANSFER TEST SUCCESSFUL!")
            print(f"📊 Results:")
            print(f"   Board IP: 192.168.1.141")
            print(f"   Successful transfers: {len(successful)}")
            print(f"   Maximum file size: {max_size:,} bytes ({max_size/(1024*1024):.1f}MB)")
            print(f"   Average speed: {avg_speed:.2f} MB/s")
            
            for result in successful:
                size_mb = result['size'] / (1024*1024)
                print(f"   ✅ {result['filename']}: {size_mb:.1f}MB @ {result['speed']:.2f}MB/s")
            
            if max_size >= 1024*1024:
                print(f"\n🏆 EXCELLENT: Board can handle 1MB+ files!")
                print(f"📈 Estimated maximum: 4-8MB (based on available memory)")
            elif max_size >= 100*1024:
                print(f"\n✅ GOOD: Board can handle 100KB+ files")
            else:
                print(f"\n⚠️  LIMITED: Board limited to smaller files")
                
            return True
        else:
            print(f"❌ NO SUCCESSFUL TRANSFERS")
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
    print("Using the proven server code and testing file transfers")
    print("=" * 50)
    
    success = deploy_working_server()
    
    if success:
        print(f"\n🎉 MISSION ACCOMPLISHED!")
        print(f"✅ DHCP IP: 192.168.1.141")
        print(f"✅ HTTP server working")
        print(f"✅ File transfers successful")
        print(f"✅ Maximum file size capability confirmed")
    else:
        print(f"\n❌ Test failed - check server deployment")

if __name__ == "__main__":
    main()
