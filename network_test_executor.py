#!/usr/bin/env python3
"""
VK-RA6M5 Network Test Executor
=============================

Based on the successful agent approach from the guide, this script will:
1. Connect to the board on COM4
2. Setup Ethernet with DHCP
3. Test memory capabilities for large files
4. Create and serve files via HTTP
5. Determine maximum file transfer size

Following the proven pattern from agents_guide.
"""

import serial
import time
import sys

def communicate_with_board():
    """Execute network testing on VK-RA6M5 board"""
    try:
        # Connect to board on COM4 (as specified by user)
        print("🔌 Connecting to VK-RA6M5 board on COM4...")
        ser = serial.Serial('COM4', 115200, timeout=3)
        print('✓ Connected to COM4')
        
        # Reset board to clean state
        print("🔄 Resetting board to clean state...")
        ser.write(b'\x03\x04')  # Ctrl+C, Ctrl+D
        time.sleep(2)
        ser.read_all()  # Clear buffer
        
        def send_cmd(cmd, wait_time=2.0):
            """Send command and get clean response"""
            print(f"📤 Sending: {cmd}")
            ser.write(cmd.encode() + b'\r\n')
            time.sleep(wait_time)  # Wait for response
            response = ser.read_all()
            if response:
                output = response.decode('utf-8', errors='ignore')
                # Filter out command echoes and prompts
                lines = output.split('\n')
                clean_lines = []
                for line in lines:
                    clean = line.strip()
                    if clean and not clean.startswith('>>>') and clean != cmd.strip() and clean != '...':
                        clean_lines.append(clean)
                if clean_lines:
                    result = '\n'.join(clean_lines)
                    print(f"📥 Response: {result}")
                    return result
            return None
        
        # Step 1: Check initial memory state
        print("\n" + "="*50)
        print("STEP 1: Initial Memory Check")
        print("="*50)
        
        send_cmd('import gc')
        send_cmd('gc.collect()')
        mem_result = send_cmd('gc.mem_free()')
        print(f"Initial free memory: {mem_result}")
        
        # Step 2: Network Setup
        print("\n" + "="*50)
        print("STEP 2: Network Setup with DHCP")
        print("="*50)
        
        send_cmd('import network')
        send_cmd('import time')
        send_cmd('lan = network.LAN()')
        send_cmd('print("LAN interface created")')
        
        send_cmd('lan.active(True)')
        send_cmd('print("Interface activated")')
        
        # Wait for DHCP with timeout
        print("⏳ Waiting for DHCP configuration...")
        dhcp_script = '''
for i in range(20):
    config = lan.ifconfig()
    if config[0] != '0.0.0.0':
        print(f"✓ DHCP Success: IP={config[0]}, Gateway={config[2]}")
        ip_address = config[0]
        break
    time.sleep(1)
    if i % 5 == 0:
        print(f"Waiting for DHCP... {20-i}s remaining")
else:
    print("✗ DHCP failed - no IP address")
    ip_address = None
'''
        send_cmd(dhcp_script, 25.0)  # Wait longer for DHCP
        
        # Get the IP address
        ip_result = send_cmd('print(f"Board IP: {ip_address}")')
        
        # Step 3: Memory and File Testing
        print("\n" + "="*50)
        print("STEP 3: Memory and File Creation Testing")
        print("="*50)
        
        # Test memory allocation capabilities
        memory_test_script = '''
import os
print("Testing memory allocation...")
test_sizes = [0.5, 1, 2, 4]  # MB
max_file_size = 0
created_files = []

for size_mb in test_sizes:
    filename = f"test_{size_mb}mb.bin"
    size_bytes = int(size_mb * 1024 * 1024)
    
    print(f"Creating {size_mb}MB file: {filename}")
    gc.collect()
    free_before = gc.mem_free()
    print(f"  Free memory before: {free_before:,} bytes")
    
    try:
        with open(filename, 'wb') as f:
            chunk_size = 8192
            written = 0
            while written < size_bytes:
                remaining = min(chunk_size, size_bytes - written)
                # Create unique pattern to prevent compression
                chunk = bytearray()
                for i in range(remaining):
                    value = (written + i) & 0xFF
                    chunk.append(value)
                f.write(chunk)
                written += remaining
                
                # Progress for larger files
                if size_mb >= 1 and written % (512 * 1024) == 0:
                    progress_mb = written / (1024 * 1024)
                    print(f"    {progress_mb:.1f}MB written...")
        
        # Verify file
        actual_size = os.stat(filename)[6]
        if actual_size == size_bytes:
            print(f"  ✓ SUCCESS: {filename} created ({actual_size:,} bytes)")
            max_file_size = size_mb
            created_files.append(filename)
        else:
            print(f"  ✗ SIZE MISMATCH: Expected {size_bytes}, got {actual_size}")
            break
            
    except Exception as e:
        print(f"  ✗ FAILED: {e}")
        break
    
    gc.collect()
    free_after = gc.mem_free()
    print(f"  Free memory after: {free_after:,} bytes")

print(f"Maximum file size created: {max_file_size}MB")
print(f"Files created: {created_files}")
'''
        send_cmd(memory_test_script, 30.0)  # Wait longer for file creation

        # Step 4: Web Server Setup
        print("\n" + "="*50)
        print("STEP 4: HTTP Web Server for File Transfer Testing")
        print("="*50)

        web_server_script = '''
import socket

def start_web_server():
    if ip_address is None:
        print("✗ Cannot start server - no IP address")
        return

    print(f"Starting HTTP server on {ip_address}:8080...")

    try:
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(('0.0.0.0', 8080))
        server.listen(1)

        print(f"✓ Server ready: http://{ip_address}:8080/")
        print("Available test files:")
        for filename in created_files:
            size = os.stat(filename)[6]
            print(f"  http://{ip_address}:8080/{filename} ({size:,} bytes)")

        print("Server will handle 10 requests then stop...")

        for request_num in range(10):
            try:
                client, addr = server.accept()
                print(f"\\nRequest #{request_num+1} from {addr[0]}")

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
                                # Serve file list
                                html = f"""<!DOCTYPE html>
<html>
<head><title>RA6M5 Network Test</title></head>
<body>
<h1>RA6M5 File Transfer Test</h1>
<p>Board IP: {ip_address}</p>
<p>Free Memory: {gc.mem_free():,} bytes</p>
<h2>Test Files:</h2>
"""
                                for filename in created_files:
                                    size = os.stat(filename)[6]
                                    size_mb = size / (1024 * 1024)
                                    html += f'<p><a href="/{filename}">{filename}</a> ({size_mb:.1f}MB)</p>\\n'

                                html += "</body></html>"

                                response = f"""HTTP/1.1 200 OK\\r
Content-Type: text/html\\r
Content-Length: {len(html)}\\r
Connection: close\\r
\\r
{html}"""
                                client.send(response.encode())

                            elif path.startswith('/test_') and path.endswith('.bin'):
                                # Serve file
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

                                    # Send file content
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
                                            if file_size > 512*1024 and sent % (256*1024) == 0:
                                                print(f"    Sent {sent//1024}KB...")

                                    elapsed = time.ticks_diff(time.ticks_ms(), start_time)
                                    if elapsed > 0:
                                        speed = (sent * 1000) / (elapsed * 1024)  # KB/s
                                        print(f"  ✓ Transfer complete: {sent:,} bytes in {elapsed}ms ({speed:.1f}KB/s)")
                                    else:
                                        print(f"  ✓ Transfer complete: {sent:,} bytes")
                                else:
                                    # 404 Not Found
                                    response = """HTTP/1.1 404 Not Found\\r
Content-Type: text/html\\r
Connection: close\\r
\\r
<h1>404 Not Found</h1>"""
                                    client.send(response.encode())
                            else:
                                # 404 for other paths
                                response = """HTTP/1.1 404 Not Found\\r
Content-Type: text/html\\r
Connection: close\\r
\\r
<h1>404 Not Found</h1>"""
                                client.send(response.encode())

                client.close()

            except Exception as e:
                print(f"  Request error: {e}")
                try:
                    client.close()
                except:
                    pass

        server.close()
        print("\\n✓ Web server completed 10 requests")

    except Exception as e:
        print(f"✗ Web server error: {e}")

# Start the web server
start_web_server()
'''
        send_cmd(web_server_script, 60.0)  # Wait longer for web server

        # Step 5: Final Results
        print("\n" + "="*50)
        print("STEP 5: Final Results Summary")
        print("="*50)

        send_cmd('print(f"Final IP Address: {ip_address}")')
        send_cmd('print(f"Maximum file size: {max_file_size}MB")')
        send_cmd('print(f"Files created: {len(created_files)}")')
        send_cmd('gc.collect()')
        send_cmd('print(f"Final free memory: {gc.mem_free():,} bytes")')

        print("\n🎉 Network testing completed!")
        print("📊 Check the output above for:")
        print("   - DHCP IP address obtained")
        print("   - Maximum file size created")
        print("   - Web server URL for testing")
        print("   - File transfer performance")

        return ser

    except Exception as e:
        print(f"❌ Error: {e}")
        return None

def main():
    """Main execution function"""
    print("🚀 VK-RA6M5 Network Testing Suite")
    print("=" * 50)
    print("This will test:")
    print("✓ Ethernet DHCP configuration")
    print("✓ Memory capabilities for large files")
    print("✓ File creation up to maximum size")
    print("✓ HTTP web server for file transfers")
    print("✓ Transfer performance measurement")
    print()

    ser = communicate_with_board()
    if ser:
        try:
            print("\n📡 Monitoring board output for 30 seconds...")
            print("(You can test the web server now)")

            # Monitor for additional output
            for i in range(30):
                if ser.in_waiting:
                    data = ser.read_all().decode('utf-8', errors='ignore')
                    if data.strip():
                        print(f"📥 Board: {data.strip()}")
                time.sleep(1)

        finally:
            ser.close()
            print("🔌 Connection closed")
    else:
        print("❌ Failed to connect to board")
        print("Check:")
        print("- Board is connected to COM4")
        print("- MicroPython is running")
        print("- No other programs using COM4")

if __name__ == "__main__":
    main()
