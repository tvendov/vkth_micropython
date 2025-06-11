#!/usr/bin/env python3
"""
Create Web Server on VK-RA6M5 Board
===================================

Now that we have the IP (192.168.1.141), let's create files and start a web server
to test the maximum file transfer capabilities.
"""

import serial
import time

def setup_web_server():
    """Setup web server with test files on the board"""
    try:
        print("🔌 Connecting to VK-RA6M5 board on COM4...")
        ser = serial.Serial('COM4', 115200, timeout=5)
        print('✓ Connected to COM4')
        
        # Reset to clean state
        ser.write(b'\x03')  # Ctrl+C
        time.sleep(1)
        ser.read_all()
        
        def send_cmd(cmd, wait_time=3.0):
            """Send command and get response"""
            print(f"📤 {cmd[:50]}{'...' if len(cmd) > 50 else ''}")
            ser.write(cmd.encode() + b'\r\n')
            time.sleep(wait_time)
            response = ser.read_all()
            if response:
                output = response.decode('utf-8', errors='ignore')
                # Extract meaningful output
                lines = output.split('\n')
                for line in lines:
                    clean = line.strip()
                    if clean and not clean.startswith('>>>') and clean != cmd.strip() and clean != '...':
                        print(f"📥 {clean}")
                return output
            return None
        
        # Setup imports
        print("\n🔧 Setting up imports...")
        send_cmd('import network')
        send_cmd('import socket')
        send_cmd('import time')
        send_cmd('import gc')
        send_cmd('import os')
        
        # Setup network
        print("\n🌐 Setting up network...")
        send_cmd('lan = network.LAN()')
        send_cmd('lan.active(True)')
        send_cmd('config = lan.ifconfig()')
        send_cmd('ip_address = config[0]')
        send_cmd('print(f"Board IP: {ip_address}")')
        
        # Create test files
        print("\n📁 Creating test files...")
        
        file_creation_script = '''
# Create test files of different sizes
test_sizes = [0.5, 1, 2, 4]  # MB
created_files = []
max_file_size = 0

for size_mb in test_sizes:
    filename = f"test_{size_mb}mb.bin"
    size_bytes = int(size_mb * 1024 * 1024)
    
    print(f"Creating {size_mb}MB file: {filename}")
    gc.collect()
    free_before = gc.mem_free()
    print(f"  Free memory: {free_before:,} bytes")
    
    try:
        with open(filename, 'wb') as f:
            # Write in 8KB chunks with unique pattern
            chunk_size = 8192
            written = 0
            while written < size_bytes:
                remaining = min(chunk_size, size_bytes - written)
                chunk = bytearray()
                for i in range(remaining):
                    # Create non-repeating pattern
                    value = (written + i) & 0xFF
                    chunk.append(value)
                f.write(chunk)
                written += remaining
                
                # Progress for larger files
                if size_mb >= 1 and written % (512 * 1024) == 0:
                    progress_mb = written / (1024 * 1024)
                    print(f"    {progress_mb:.1f}MB written")
        
        # Verify file
        actual_size = os.stat(filename)[6]
        if actual_size == size_bytes:
            print(f"  ✓ SUCCESS: {filename} ({actual_size:,} bytes)")
            created_files.append(filename)
            max_file_size = size_mb
        else:
            print(f"  ✗ SIZE MISMATCH: Expected {size_bytes}, got {actual_size}")
            break
            
    except Exception as e:
        print(f"  ✗ FAILED: {e}")
        break

print(f"Maximum file size created: {max_file_size}MB")
print(f"Files created: {created_files}")
'''
        
        send_cmd(file_creation_script, 30.0)
        
        # Create web server
        print("\n🌐 Creating web server...")
        
        web_server_script = '''
def start_web_server():
    print(f"Starting HTTP server on {ip_address}:8080...")
    
    try:
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(('0.0.0.0', 8080))
        server.listen(2)
        
        print(f"✓ Server ready: http://{ip_address}:8080/")
        print("Available files:")
        for filename in created_files:
            size = os.stat(filename)[6]
            size_mb = size / (1024 * 1024)
            print(f"  http://{ip_address}:8080/{filename} ({size_mb:.1f}MB)")
        
        print("\\nServer will handle requests. Press Ctrl+C to stop.")
        
        request_count = 0
        while request_count < 20:  # Handle up to 20 requests
            try:
                client, addr = server.accept()
                request_count += 1
                print(f"\\nRequest #{request_count} from {addr[0]}")
                
                # Read request
                request = client.recv(1024).decode('utf-8')
                if not request:
                    client.close()
                    continue
                
                # Parse request
                lines = request.split('\\n')
                if lines:
                    request_line = lines[0].strip()
                    parts = request_line.split(' ')
                    if len(parts) >= 2:
                        method = parts[0]
                        path = parts[1]
                        print(f"  {method} {path}")
                        
                        if method == 'GET':
                            if path == '/':
                                # Serve file list page
                                html = f"""<!DOCTYPE html>
<html>
<head>
    <title>RA6M5 Network Test</title>
    <style>
        body {{ font-family: Arial, sans-serif; margin: 40px; }}
        .file-link {{ display: block; margin: 10px 0; padding: 10px; 
                     background: #f0f0f0; text-decoration: none; border-radius: 5px; }}
        .file-link:hover {{ background: #e0e0e0; }}
        .info {{ background: #e7f3ff; padding: 15px; border-radius: 5px; margin: 20px 0; }}
    </style>
</head>
<body>
    <h1>RA6M5 Network File Transfer Test</h1>
    
    <div class="info">
        <strong>Board IP:</strong> {ip_address}<br>
        <strong>Free Memory:</strong> {gc.mem_free():,} bytes ({gc.mem_free()/(1024*1024):.1f}MB)<br>
        <strong>Max File Size:</strong> {max_file_size}MB
    </div>
    
    <h2>Test Files for Download</h2>
"""
                                
                                for filename in created_files:
                                    size = os.stat(filename)[6]
                                    size_mb = size / (1024 * 1024)
                                    html += f'<a href="/{filename}" class="file-link">{filename} ({size_mb:.1f}MB - {size:,} bytes)</a>\\n'
                                
                                html += """
    <div class="info">
        <p><strong>Instructions:</strong></p>
        <ul>
            <li>Click links above to download test files</li>
            <li>Monitor transfer speeds in your browser</li>
            <li>Test with multiple simultaneous downloads</li>
            <li>Use tools like wget or curl for performance testing</li>
        </ul>
    </div>
</body>
</html>"""
                                
                                response = f"""HTTP/1.1 200 OK\\r
Content-Type: text/html\\r
Content-Length: {len(html)}\\r
Connection: close\\r
\\r
{html}"""
                                client.send(response.encode())
                                
                            elif path.startswith('/test_') and path.endswith('.bin'):
                                # Serve file download
                                filename = path[1:]  # Remove leading '/'
                                if filename in created_files:
                                    file_size = os.stat(filename)[6]
                                    print(f"  Serving {filename} ({file_size:,} bytes)")
                                    
                                    # Send headers
                                    headers = f"""HTTP/1.1 200 OK\\r
Content-Type: application/octet-stream\\r
Content-Length: {file_size}\\r
Content-Disposition: attachment; filename="{filename}"\\r
Connection: close\\r
\\r
"""
                                    client.send(headers.encode())
                                    
                                    # Send file content with progress tracking
                                    with open(filename, 'rb') as f:
                                        sent = 0
                                        start_time = time.ticks_ms()
                                        chunk_size = 8192
                                        
                                        while sent < file_size:
                                            chunk = f.read(chunk_size)
                                            if not chunk:
                                                break
                                            client.send(chunk)
                                            sent += len(chunk)
                                            
                                            # Progress for large files
                                            if file_size > 512*1024 and sent % (256*1024) == 0:
                                                elapsed = time.ticks_diff(time.ticks_ms(), start_time)
                                                if elapsed > 0:
                                                    speed = (sent * 1000) / (elapsed * 1024)  # KB/s
                                                    print(f"    Sent {sent//1024}KB ({speed:.1f}KB/s)")
                                    
                                    elapsed = time.ticks_diff(time.ticks_ms(), start_time)
                                    if elapsed > 0:
                                        speed = (sent * 1000) / (elapsed * 1024)  # KB/s
                                        speed_mb = speed / 1024  # MB/s
                                        print(f"  ✓ Transfer complete: {sent:,} bytes in {elapsed}ms ({speed:.1f}KB/s = {speed_mb:.2f}MB/s)")
                                    else:
                                        print(f"  ✓ Transfer complete: {sent:,} bytes")
                                else:
                                    # File not found
                                    response = """HTTP/1.1 404 Not Found\\r
Content-Type: text/html\\r
Connection: close\\r
\\r
<h1>404 File Not Found</h1>"""
                                    client.send(response.encode())
                            else:
                                # Other paths - 404
                                response = """HTTP/1.1 404 Not Found\\r
Content-Type: text/html\\r
Connection: close\\r
\\r
<h1>404 Not Found</h1>"""
                                client.send(response.encode())
                
                client.close()
                
            except KeyboardInterrupt:
                print("\\nServer stopped by user")
                break
            except Exception as e:
                print(f"  Request error: {e}")
                try:
                    client.close()
                except:
                    pass
        
        server.close()
        print(f"\\n✓ Web server completed {request_count} requests")
        
    except Exception as e:
        print(f"✗ Web server error: {e}")

# Start the web server
start_web_server()
'''
        
        send_cmd(web_server_script, 5.0)
        
        print(f"\n🎉 Web server should now be running!")
        print(f"🌐 Open your browser to: http://192.168.1.141:8080/")
        print(f"📊 Test file downloads to measure maximum transfer speeds")
        
        # Monitor server output
        print(f"\n📡 Monitoring server activity...")
        for i in range(300):  # Monitor for 5 minutes
            if ser.in_waiting:
                data = ser.read_all().decode('utf-8', errors='ignore')
                if data.strip():
                    lines = data.strip().split('\n')
                    for line in lines:
                        if line.strip():
                            print(f"📥 {line.strip()}")
            time.sleep(1)
        
        ser.close()
        
    except Exception as e:
        print(f"❌ Error: {e}")
        try:
            ser.close()
        except:
            pass

if __name__ == "__main__":
    print("🚀 Setting up web server on VK-RA6M5 board")
    print("IP Address: 192.168.1.141")
    setup_web_server()
