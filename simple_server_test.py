#!/usr/bin/env python3
"""
Simple Server Test for VK-RA6M5
===============================

Create a minimal web server and test file transfers step by step.
"""

import serial
import time

def run_simple_server_test():
    """Run a simple server test"""
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
                    if clean and not clean.startswith('>>>') and clean != cmd.strip():
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
        
        # Create one test file
        print("\n📁 Creating test file...")
        create_file_cmd = '''
filename = "test_1mb.bin"
size_bytes = 1024 * 1024  # 1MB
print(f"Creating {filename}...")

with open(filename, 'wb') as f:
    for i in range(size_bytes):
        f.write(bytes([i & 0xFF]))

actual_size = os.stat(filename)[6]
print(f"Created: {actual_size:,} bytes")
'''
        send_cmd(create_file_cmd, 10.0)
        
        # Simple server
        print("\n🌐 Starting simple server...")
        server_cmd = '''
print(f"Starting server on {ip}:8080...")

server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind(('0.0.0.0', 8080))
server.listen(1)

print(f"Server ready: http://{ip}:8080/")
print("Waiting for requests...")

for i in range(5):  # Handle 5 requests
    try:
        client, addr = server.accept()
        print(f"Request from {addr[0]}")
        
        request = client.recv(1024).decode('utf-8')
        
        if 'GET /' in request and 'test_1mb.bin' not in request:
            # Serve index page
            html = f"""<!DOCTYPE html>
<html>
<head><title>RA6M5 Test</title></head>
<body>
<h1>RA6M5 Network Test</h1>
<p>Board IP: {ip}</p>
<p><a href="/test_1mb.bin">Download 1MB test file</a></p>
</body>
</html>"""
            
            response = f"""HTTP/1.1 200 OK\\r
Content-Type: text/html\\r
Content-Length: {len(html)}\\r
Connection: close\\r
\\r
{html}"""
            client.send(response.encode())
            
        elif 'test_1mb.bin' in request:
            # Serve file
            file_size = os.stat(filename)[6]
            print(f"Serving {filename} ({file_size:,} bytes)")
            
            headers = f"""HTTP/1.1 200 OK\\r
Content-Type: application/octet-stream\\r
Content-Length: {file_size}\\r
Content-Disposition: attachment; filename="{filename}"\\r
Connection: close\\r
\\r
"""
            client.send(headers.encode())
            
            # Send file
            with open(filename, 'rb') as f:
                sent = 0
                start_time = time.ticks_ms()
                while sent < file_size:
                    chunk = f.read(8192)
                    if not chunk:
                        break
                    client.send(chunk)
                    sent += len(chunk)
                    
                    if sent % (256*1024) == 0:
                        print(f"Sent {sent//1024}KB")
            
            elapsed = time.ticks_diff(time.ticks_ms(), start_time)
            if elapsed > 0:
                speed = (sent * 1000) / (elapsed * 1024)  # KB/s
                print(f"Transfer complete: {sent:,} bytes in {elapsed}ms ({speed:.1f}KB/s)")
        
        client.close()
        
    except Exception as e:
        print(f"Request error: {e}")

server.close()
print("Server stopped")
'''
        
        send_cmd(server_cmd, 5.0)
        
        print(f"\n🎉 Server should be running!")
        print(f"🌐 Try: http://192.168.1.141:8080/")
        
        # Monitor for server activity
        print(f"\n📡 Monitoring server activity for 60 seconds...")
        for i in range(60):
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
    print("🚀 Simple Server Test for VK-RA6M5")
    print("This will create a 1MB file and serve it via HTTP")
    run_simple_server_test()
