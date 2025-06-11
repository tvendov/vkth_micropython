"""
Simple Web Server for RA6M5 Network Testing
==========================================

A lightweight HTTP server for testing file transfers over Ethernet.
This script focuses on serving files and measuring transfer performance.
"""

import network
import socket
import time
import os
import gc

class SimpleWebServer:
    def __init__(self, port=8080):
        self.port = port
        self.lan = None
        self.ip_address = None
        self.server_socket = None
        
    def setup_network(self):
        """Setup Ethernet with DHCP"""
        print("Initializing Ethernet...")
        
        try:
            self.lan = network.LAN()
            self.lan.active(True)
            
            # Wait for connection
            timeout = 15
            while timeout > 0:
                config = self.lan.ifconfig()
                if config[0] != '0.0.0.0':
                    self.ip_address = config[0]
                    break
                time.sleep(1)
                timeout -= 1
                print(".", end="")
            
            if not self.ip_address:
                print("\nERROR: Failed to get IP via DHCP")
                return False
                
            print(f"\n✓ Network ready: {self.ip_address}")
            print(f"  Gateway: {config[2]}")
            return True
            
        except Exception as e:
            print(f"Network setup failed: {e}")
            return False
    
    def create_large_file(self, size_mb, filename):
        """Create a large test file with unique content"""
        print(f"Creating {size_mb}MB file: {filename}")
        
        size_bytes = int(size_mb * 1024 * 1024)
        
        try:
            with open(filename, 'wb') as f:
                chunk_size = 4096
                written = 0
                
                while written < size_bytes:
                    remaining = min(chunk_size, size_bytes - written)
                    
                    # Create unique pattern
                    chunk = bytearray()
                    for i in range(remaining):
                        # Non-repeating pattern based on position
                        value = (written + i) & 0xFF
                        chunk.append(value)
                    
                    f.write(chunk)
                    written += remaining
                    
                    # Progress indicator
                    if written % (1024 * 1024) == 0:
                        mb_written = written // (1024 * 1024)
                        print(f"  {mb_written}MB written...")
                        
            print(f"✓ Created {filename} ({size_bytes} bytes)")
            return True
            
        except Exception as e:
            print(f"ERROR creating file: {e}")
            return False
    
    def start_server(self):
        """Start the HTTP server"""
        try:
            self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.server_socket.bind(('0.0.0.0', self.port))
            self.server_socket.listen(5)
            
            print(f"✓ Server started: http://{self.ip_address}:{self.port}/")
            return True
            
        except Exception as e:
            print(f"Server start failed: {e}")
            return False
    
    def handle_request(self, client_socket, client_addr):
        """Handle HTTP request"""
        try:
            request = client_socket.recv(1024).decode('utf-8')
            if not request:
                return
                
            lines = request.split('\n')
            request_line = lines[0].strip()
            method, path, _ = request_line.split(' ', 2)
            
            print(f"{client_addr[0]} - {method} {path}")
            
            if path == '/':
                self.serve_index(client_socket)
            elif path.startswith('/download/'):
                filename = path[10:]  # Remove '/download/'
                self.serve_file(client_socket, filename)
            elif path.startswith('/create/'):
                # Create file on demand: /create/5mb
                size_str = path[8:]
                if size_str.endswith('mb'):
                    size_mb = float(size_str[:-2])
                    filename = f"test_{size_str}.bin"
                    self.create_and_serve(client_socket, size_mb, filename)
                else:
                    self.serve_404(client_socket)
            else:
                self.serve_404(client_socket)
                
        except Exception as e:
            print(f"Request error: {e}")
        finally:
            client_socket.close()
    
    def serve_index(self, client_socket):
        """Serve main page with file creation links"""
        html = f"""<!DOCTYPE html>
<html>
<head>
    <title>RA6M5 File Transfer Test</title>
    <style>
        body {{ font-family: Arial, sans-serif; margin: 40px; }}
        .test-link {{ display: block; margin: 10px 0; padding: 10px; 
                     background: #f0f0f0; text-decoration: none; border-radius: 5px; }}
        .test-link:hover {{ background: #e0e0e0; }}
        .info {{ background: #e7f3ff; padding: 15px; border-radius: 5px; margin: 20px 0; }}
    </style>
</head>
<body>
    <h1>RA6M5 Network File Transfer Test</h1>
    
    <div class="info">
        <strong>Board IP:</strong> {self.ip_address}<br>
        <strong>Free Memory:</strong> {gc.mem_free():,} bytes ({gc.mem_free()/(1024*1024):.1f}MB)
    </div>
    
    <h2>Test File Downloads</h2>
    <p>Click links below to create and download test files:</p>
    
    <a href="/create/0.5mb" class="test-link">Create & Download 0.5MB file</a>
    <a href="/create/1mb" class="test-link">Create & Download 1MB file</a>
    <a href="/create/2mb" class="test-link">Create & Download 2MB file</a>
    <a href="/create/4mb" class="test-link">Create & Download 4MB file</a>
    <a href="/create/6mb" class="test-link">Create & Download 6MB file</a>
    <a href="/create/8mb" class="test-link">Create & Download 8MB file</a>
    
    <h2>Existing Files</h2>
"""
        
        # List existing files
        try:
            files = os.listdir('.')
            for filename in files:
                if filename.endswith('.bin'):
                    try:
                        size = os.stat(filename)[6]
                        size_mb = size / (1024 * 1024)
                        html += f'<a href="/download/{filename}" class="test-link">{filename} ({size_mb:.1f}MB)</a>\n'
                    except:
                        pass
        except:
            pass
            
        html += """
    <div class="info">
        <p><strong>Instructions:</strong></p>
        <ul>
            <li>Use browser download or tools like wget/curl to test transfer speeds</li>
            <li>Monitor transfer progress in the console</li>
            <li>Test maximum file sizes your network can handle</li>
        </ul>
    </div>
</body>
</html>"""
        
        response = f"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: {len(html)}\r\nConnection: close\r\n\r\n{html}"
        client_socket.send(response.encode('utf-8'))
    
    def create_and_serve(self, client_socket, size_mb, filename):
        """Create file and serve it immediately"""
        if self.create_large_file(size_mb, filename):
            self.serve_file(client_socket, filename)
        else:
            self.serve_error(client_socket, "Failed to create file")
    
    def serve_file(self, client_socket, filename):
        """Serve file for download"""
        try:
            if not os.path.exists(filename):
                self.serve_404(client_socket)
                return
                
            file_size = os.stat(filename)[6]
            
            headers = f"""HTTP/1.1 200 OK\r
Content-Type: application/octet-stream\r
Content-Length: {file_size}\r
Content-Disposition: attachment; filename="{filename}"\r
Connection: close\r
\r
"""
            client_socket.send(headers.encode('utf-8'))
            
            # Send file in chunks
            with open(filename, 'rb') as f:
                sent = 0
                chunk_size = 8192
                start_time = time.time()
                
                while sent < file_size:
                    chunk = f.read(chunk_size)
                    if not chunk:
                        break
                    client_socket.send(chunk)
                    sent += len(chunk)
                    
                    # Progress for large files
                    if file_size > 1024*1024 and sent % (1024*1024) == 0:
                        elapsed = time.time() - start_time
                        speed = (sent / elapsed) / (1024*1024) if elapsed > 0 else 0
                        print(f"  Sent {sent//(1024*1024)}MB/{file_size//(1024*1024)}MB ({speed:.1f}MB/s)")
                
                elapsed = time.time() - start_time
                speed = (sent / elapsed) / (1024*1024) if elapsed > 0 else 0
                print(f"✓ Transfer complete: {filename} ({sent} bytes, {speed:.1f}MB/s)")
                
        except Exception as e:
            print(f"File serve error: {e}")
    
    def serve_404(self, client_socket):
        """Serve 404 response"""
        response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n<h1>404 Not Found</h1>"
        client_socket.send(response.encode('utf-8'))
    
    def serve_error(self, client_socket, message):
        """Serve error response"""
        response = f"HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n<h1>Error: {message}</h1>"
        client_socket.send(response.encode('utf-8'))
    
    def run(self):
        """Main server loop"""
        if not self.setup_network():
            return
            
        if not self.start_server():
            return
            
        print("Server running. Press Ctrl+C to stop.")
        print(f"Open http://{self.ip_address}:{self.port}/ in your browser")
        
        try:
            while True:
                client_socket, client_addr = self.server_socket.accept()
                self.handle_request(client_socket, client_addr)
        except KeyboardInterrupt:
            print("\nShutting down server...")
        finally:
            if self.server_socket:
                self.server_socket.close()

# Run the server
if __name__ == "__main__":
    server = SimpleWebServer()
    server.run()
