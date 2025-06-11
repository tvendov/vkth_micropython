"""
Manual Network Test for RA6M5
=============================

Copy and paste this code directly into the MicroPython REPL
to test network functionality and file transfer capabilities.
"""

# Quick Network Setup and Test
print("RA6M5 Network Test - Manual Mode")
print("="*40)

import network
import socket
import time
import gc
import os

# Step 1: Setup Network
print("1. Setting up network...")
try:
    lan = network.LAN()
    lan.active(True)
    print("   LAN interface activated")
    
    # Wait for DHCP
    timeout = 15
    while timeout > 0:
        config = lan.ifconfig()
        if config[0] != '0.0.0.0':
            break
        time.sleep(1)
        timeout -= 1
        if timeout % 5 == 0:
            print(f"   Waiting for DHCP... {timeout}s")
    
    if config[0] == '0.0.0.0':
        print("   ERROR: No IP address obtained")
    else:
        ip = config[0]
        print(f"   ✓ IP Address: {ip}")
        print(f"   ✓ Gateway: {config[2]}")
        
        # Step 2: Memory Check
        print("\n2. Checking memory...")
        gc.collect()
        free_mem = gc.mem_free()
        print(f"   Free memory: {free_mem:,} bytes ({free_mem/(1024*1024):.1f}MB)")
        
        # Step 3: Create test file
        print("\n3. Creating test file...")
        test_size = 1  # 1MB
        filename = f"test_{test_size}mb.bin"
        size_bytes = test_size * 1024 * 1024
        
        try:
            with open(filename, 'wb') as f:
                for i in range(size_bytes):
                    f.write(bytes([i & 0xFF]))
            
            actual_size = os.stat(filename)[6]
            print(f"   ✓ Created {filename}: {actual_size:,} bytes")
            
            # Step 4: Simple HTTP Server
            print(f"\n4. Starting HTTP server on {ip}:8080...")
            
            server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server.bind(('0.0.0.0', 8080))
            server.listen(1)
            
            print(f"   ✓ Server ready: http://{ip}:8080/")
            print("   Open this URL in your browser to download the test file")
            print("   Press Ctrl+C to stop the server")
            
            # Simple server loop
            request_count = 0
            while request_count < 10:  # Handle up to 10 requests
                try:
                    client, addr = server.accept()
                    print(f"\n   Connection from {addr[0]}")
                    
                    # Read request
                    request = client.recv(1024).decode('utf-8')
                    
                    # Send file
                    if 'GET' in request:
                        # Send headers
                        headers = f"""HTTP/1.1 200 OK\r
Content-Type: application/octet-stream\r
Content-Length: {actual_size}\r
Content-Disposition: attachment; filename="{filename}"\r
Connection: close\r
\r
"""
                        client.send(headers.encode())
                        
                        # Send file content
                        with open(filename, 'rb') as f:
                            sent = 0
                            start_time = time.time()
                            while sent < actual_size:
                                chunk = f.read(8192)
                                if not chunk:
                                    break
                                client.send(chunk)
                                sent += len(chunk)
                                
                                # Progress
                                if sent % (256*1024) == 0:
                                    elapsed = time.time() - start_time
                                    speed = (sent / elapsed) / (1024*1024) if elapsed > 0 else 0
                                    print(f"     Sent {sent//1024}KB ({speed:.1f}MB/s)")
                        
                        elapsed = time.time() - start_time
                        speed = (sent / elapsed) / (1024*1024) if elapsed > 0 else 0
                        print(f"   ✓ Transfer complete: {sent:,} bytes in {elapsed:.1f}s ({speed:.1f}MB/s)")
                    
                    client.close()
                    request_count += 1
                    
                except KeyboardInterrupt:
                    break
                except Exception as e:
                    print(f"   Request error: {e}")
            
            server.close()
            print("\n   Server stopped")
            
            # Cleanup
            os.remove(filename)
            print(f"   Cleaned up {filename}")
            
        except Exception as e:
            print(f"   File creation failed: {e}")
            
except Exception as e:
    print(f"Network setup failed: {e}")

print("\nTest completed!")

# Additional quick tests you can run separately:

def quick_memory_test():
    """Quick memory allocation test"""
    print("Quick Memory Test:")
    gc.collect()
    initial = gc.mem_free()
    print(f"  Initial free: {initial:,} bytes")
    
    # Try to allocate 1MB
    try:
        data = bytearray(1024*1024)
        print("  ✓ 1MB allocation successful")
        del data
        gc.collect()
        print(f"  Final free: {gc.mem_free():,} bytes")
    except:
        print("  ✗ 1MB allocation failed")

def quick_file_test():
    """Quick file creation test"""
    print("Quick File Test:")
    sizes = [100, 500, 1000]  # KB
    for size_kb in sizes:
        try:
            filename = f"test_{size_kb}kb.bin"
            with open(filename, 'wb') as f:
                f.write(b'x' * (size_kb * 1024))
            actual = os.stat(filename)[6]
            print(f"  ✓ {size_kb}KB file: {actual:,} bytes")
            os.remove(filename)
        except Exception as e:
            print(f"  ✗ {size_kb}KB file failed: {e}")

def quick_network_info():
    """Quick network information"""
    print("Network Information:")
    try:
        lan = network.LAN()
        if lan.active():
            config = lan.ifconfig()
            print(f"  IP: {config[0]}")
            print(f"  Mask: {config[1]}")
            print(f"  Gateway: {config[2]}")
            print(f"  DNS: {config[3]}")
        else:
            print("  Network not active")
    except Exception as e:
        print(f"  Error: {e}")

# Uncomment these lines to run additional tests:
# quick_memory_test()
# quick_file_test()
# quick_network_info()
