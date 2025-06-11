"""
RA6M5 Network Testing Suite
==========================

This script tests the maximum file transfer capabilities of the VK-RA6M5 board
using Ethernet connection with DHCP and a simple web server.

Features:
- Automatic DHCP configuration
- Large file generation in OSPI RAM
- HTTP web server for file transfers
- Progressive file size testing
"""

import network
import socket
import time
import gc
import machine
import os
import sys

# Network configuration
DHCP_TIMEOUT = 30  # seconds
WEB_SERVER_PORT = 8080

class NetworkTester:
    def __init__(self):
        self.lan = None
        self.ip_address = None
        self.test_files = {}
        self.max_file_size = 0
        
    def setup_network(self):
        """Initialize Ethernet and configure DHCP"""
        print("Setting up Ethernet connection...")
        
        try:
            # Initialize LAN interface
            self.lan = network.LAN()
            print(f"LAN interface created: {self.lan}")
            
            # Activate the interface
            self.lan.active(True)
            print("LAN interface activated")
            
            # Wait for link to come up
            print("Waiting for Ethernet link...")
            link_timeout = 10
            while not self.lan.isconnected() and link_timeout > 0:
                time.sleep(1)
                link_timeout -= 1
                print(".", end="")
            print()
            
            if not self.lan.isconnected():
                print("Warning: Ethernet link not detected, continuing anyway...")
            
            # Get network configuration
            config = self.lan.ifconfig()
            self.ip_address = config[0]
            
            print(f"Network Configuration:")
            print(f"  IP Address: {config[0]}")
            print(f"  Subnet Mask: {config[1]}")
            print(f"  Gateway: {config[2]}")
            print(f"  DNS: {config[3]}")
            
            if config[0] == '0.0.0.0':
                print("ERROR: Failed to get IP address via DHCP")
                return False
                
            print(f"✓ Network setup successful! IP: {self.ip_address}")
            return True
            
        except Exception as e:
            print(f"ERROR: Network setup failed: {e}")
            return False
    
    def create_test_file(self, size_mb, filename=None):
        """Create a test file of specified size in MB"""
        if filename is None:
            filename = f"test_{size_mb}mb.bin"
            
        size_bytes = size_mb * 1024 * 1024
        
        print(f"Creating {size_mb}MB test file: {filename}")
        
        try:
            # Create file with unique pattern to ensure it's not compressed
            with open(filename, 'wb') as f:
                chunk_size = 8192  # 8KB chunks
                pattern_base = 0
                
                for i in range(0, size_bytes, chunk_size):
                    remaining = min(chunk_size, size_bytes - i)
                    
                    # Create unique pattern for each chunk
                    chunk = bytearray()
                    for j in range(remaining):
                        # Create non-repeating pattern
                        value = (pattern_base + i + j) & 0xFF
                        chunk.append(value)
                    
                    f.write(chunk)
                    
                    # Show progress for large files
                    if size_mb >= 1 and i % (1024 * 1024) == 0:
                        progress = (i // (1024 * 1024)) + 1
                        print(f"  Progress: {progress}MB written")
                        
            self.test_files[filename] = size_bytes
            print(f"✓ Created {filename} ({size_bytes} bytes)")
            return filename
            
        except Exception as e:
            print(f"ERROR: Failed to create {filename}: {e}")
            return None
    
    def verify_file_integrity(self, filename):
        """Verify file was created correctly and contains expected data"""
        try:
            size = os.stat(filename)[6]  # Get file size
            print(f"Verifying {filename} ({size} bytes)...")
            
            # Read and verify first and last chunks
            with open(filename, 'rb') as f:
                # Check first 1KB
                first_chunk = f.read(1024)
                if len(first_chunk) != 1024:
                    print(f"ERROR: First chunk size mismatch")
                    return False
                
                # Check pattern in first chunk
                for i, byte_val in enumerate(first_chunk):
                    expected = i & 0xFF
                    if byte_val != expected:
                        print(f"ERROR: Pattern mismatch at position {i}")
                        return False
                
                # Check last 1KB if file is large enough
                if size > 1024:
                    f.seek(-1024, 2)  # Seek to 1KB from end
                    last_chunk = f.read(1024)
                    if len(last_chunk) != 1024:
                        print(f"ERROR: Last chunk size mismatch")
                        return False
                
            print(f"✓ File integrity verified")
            return True

        except Exception as e:
            print(f"ERROR: File verification failed: {e}")
            return False

    def start_web_server(self):
        """Start HTTP server for file downloads"""
        if not self.ip_address:
            print("ERROR: No IP address available")
            return None

        try:
            # Create socket
            server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

            # Bind to all interfaces
            server_socket.bind(('0.0.0.0', WEB_SERVER_PORT))
            server_socket.listen(5)

            print(f"✓ Web server started on http://{self.ip_address}:{WEB_SERVER_PORT}/")
            print("Available files:")
            for filename, size in self.test_files.items():
                size_mb = size / (1024 * 1024)
                print(f"  http://{self.ip_address}:{WEB_SERVER_PORT}/{filename} ({size_mb:.1f}MB)")

            return server_socket

        except Exception as e:
            print(f"ERROR: Failed to start web server: {e}")
            return None

    def handle_http_request(self, client_socket, client_addr):
        """Handle a single HTTP request"""
        try:
            # Receive request
            request = client_socket.recv(1024).decode('utf-8')
            if not request:
                return

            # Parse request line
            lines = request.split('\n')
            if not lines:
                return

            request_line = lines[0].strip()
            parts = request_line.split(' ')
            if len(parts) < 2:
                return

            method = parts[0]
            path = parts[1]

            print(f"Request from {client_addr}: {method} {path}")

            if method == 'GET':
                if path == '/':
                    # Serve file list
                    self.serve_file_list(client_socket)
                elif path.startswith('/test_') and path.endswith('.bin'):
                    # Serve test file
                    filename = path[1:]  # Remove leading '/'
                    self.serve_file(client_socket, filename)
                else:
                    # 404 Not Found
                    self.serve_404(client_socket)
            else:
                # Method not allowed
                self.serve_405(client_socket)

        except Exception as e:
            print(f"ERROR handling request from {client_addr}: {e}")
        finally:
            try:
                client_socket.close()
            except:
                pass

    def serve_file_list(self, client_socket):
        """Serve HTML page with file list"""
        html = """<!DOCTYPE html>
<html>
<head>
    <title>RA6M5 Network Test Files</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 40px; }
        .file-list { margin: 20px 0; }
        .file-item { margin: 10px 0; padding: 10px; border: 1px solid #ccc; }
        .download-btn { background: #007cba; color: white; padding: 8px 16px; text-decoration: none; border-radius: 4px; }
    </style>
</head>
<body>
    <h1>RA6M5 Network Test Files</h1>
    <p>Board IP: """ + self.ip_address + """</p>
    <div class="file-list">
"""

        for filename, size in self.test_files.items():
            size_mb = size / (1024 * 1024)
            html += f"""
        <div class="file-item">
            <strong>{filename}</strong> ({size_mb:.1f}MB, {size:,} bytes)<br>
            <a href="/{filename}" class="download-btn">Download</a>
        </div>
"""

        html += """
    </div>
    <p><em>Use these files to test maximum transfer speeds and sizes.</em></p>
</body>
</html>
"""

        response = f"""HTTP/1.1 200 OK\r
Content-Type: text/html\r
Content-Length: {len(html)}\r
Connection: close\r
\r
{html}"""

        client_socket.send(response.encode('utf-8'))

    def serve_file(self, client_socket, filename):
        """Serve a test file for download"""
        if filename not in self.test_files:
            self.serve_404(client_socket)
            return

        try:
            file_size = self.test_files[filename]

            # Send headers
            headers = f"""HTTP/1.1 200 OK\r
Content-Type: application/octet-stream\r
Content-Length: {file_size}\r
Content-Disposition: attachment; filename="{filename}"\r
Connection: close\r
\r
"""
            client_socket.send(headers.encode('utf-8'))

            # Send file content in chunks
            with open(filename, 'rb') as f:
                bytes_sent = 0
                chunk_size = 8192

                while bytes_sent < file_size:
                    chunk = f.read(chunk_size)
                    if not chunk:
                        break

                    client_socket.send(chunk)
                    bytes_sent += len(chunk)

                    # Show progress for large files
                    if file_size > 1024*1024 and bytes_sent % (1024*1024) == 0:
                        progress_mb = bytes_sent // (1024*1024)
                        total_mb = file_size // (1024*1024)
                        print(f"  Sent {progress_mb}MB / {total_mb}MB")

            print(f"✓ Served {filename} ({bytes_sent} bytes)")

        except Exception as e:
            print(f"ERROR serving {filename}: {e}")

    def serve_404(self, client_socket):
        """Serve 404 Not Found response"""
        response = """HTTP/1.1 404 Not Found\r
Content-Type: text/html\r
Connection: close\r
\r
<html><body><h1>404 Not Found</h1></body></html>"""
        client_socket.send(response.encode('utf-8'))

    def serve_405(self, client_socket):
        """Serve 405 Method Not Allowed response"""
        response = """HTTP/1.1 405 Method Not Allowed\r
Content-Type: text/html\r
Connection: close\r
\r
<html><body><h1>405 Method Not Allowed</h1></body></html>"""
        client_socket.send(response.encode('utf-8'))

    def run_server(self, server_socket):
        """Run the web server loop"""
        print("Web server running. Press Ctrl+C to stop.")
        print("Test by downloading files from a web browser or using curl/wget")

        try:
            while True:
                try:
                    client_socket, client_addr = server_socket.accept()
                    self.handle_http_request(client_socket, client_addr)
                except KeyboardInterrupt:
                    break
                except Exception as e:
                    print(f"ERROR in server loop: {e}")

        finally:
            server_socket.close()
            print("Web server stopped")

    def test_progressive_file_sizes(self):
        """Test creating files of increasing sizes to find maximum"""
        print("\n=== Progressive File Size Testing ===")

        # Test sizes in MB
        test_sizes = [0.1, 0.5, 1, 2, 4, 6, 8]  # Start small and increase

        for size_mb in test_sizes:
            print(f"\nTesting {size_mb}MB file creation...")

            # Check available memory
            gc.collect()
            free_mem = gc.mem_free()
            print(f"Free memory: {free_mem:,} bytes ({free_mem/(1024*1024):.1f}MB)")

            filename = self.create_test_file(size_mb)
            if filename:
                if self.verify_file_integrity(filename):
                    self.max_file_size = max(self.max_file_size, size_mb)
                    print(f"✓ {size_mb}MB file test PASSED")
                else:
                    print(f"✗ {size_mb}MB file test FAILED (integrity check)")
                    break
            else:
                print(f"✗ {size_mb}MB file test FAILED (creation)")
                break

        print(f"\nMaximum successful file size: {self.max_file_size}MB")
        return self.max_file_size

    def cleanup_test_files(self):
        """Remove all test files"""
        print("\nCleaning up test files...")
        for filename in list(self.test_files.keys()):
            try:
                os.remove(filename)
                print(f"Removed {filename}")
            except:
                print(f"Failed to remove {filename}")
        self.test_files.clear()

def main():
    """Main test function"""
    print("=" * 50)
    print("RA6M5 Network Testing Suite")
    print("=" * 50)

    tester = NetworkTester()

    # Step 1: Setup network
    if not tester.setup_network():
        print("FAILED: Network setup unsuccessful")
        return

    # Step 2: Test file creation capabilities
    max_size = tester.test_progressive_file_sizes()
    if max_size == 0:
        print("FAILED: No files could be created")
        return

    # Step 3: Start web server
    server_socket = tester.start_web_server()
    if not server_socket:
        print("FAILED: Could not start web server")
        return

    print(f"\n=== Test Results Summary ===")
    print(f"Network IP: {tester.ip_address}")
    print(f"Maximum file size: {max_size}MB")
    print(f"Web server: http://{tester.ip_address}:{WEB_SERVER_PORT}/")
    print(f"Total test files: {len(tester.test_files)}")

    # Step 4: Run server
    try:
        tester.run_server(server_socket)
    except KeyboardInterrupt:
        print("\nShutting down...")
    finally:
        # Cleanup
        tester.cleanup_test_files()
        print("Test completed.")

if __name__ == "__main__":
    main()
