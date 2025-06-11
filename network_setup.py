"""
Quick Network Setup and Test for RA6M5
======================================

This script quickly sets up the Ethernet connection and tests basic connectivity.
Use this to verify network functionality before running larger tests.
"""

import network
import socket
import time
import gc

def test_network_setup():
    """Test basic network setup and connectivity"""
    print("=" * 40)
    print("RA6M5 Network Setup Test")
    print("=" * 40)
    
    # Step 1: Initialize LAN interface
    print("1. Initializing LAN interface...")
    try:
        lan = network.LAN()
        print(f"   LAN object created: {lan}")
        
        # Activate interface
        lan.active(True)
        print("   Interface activated")
        
        # Wait for DHCP
        print("2. Waiting for DHCP configuration...")
        timeout = 20
        while timeout > 0:
            config = lan.ifconfig()
            if config[0] != '0.0.0.0':
                break
            time.sleep(1)
            timeout -= 1
            if timeout % 5 == 0:
                print(f"   Waiting... ({timeout}s remaining)")
        
        if config[0] == '0.0.0.0':
            print("   ERROR: DHCP failed - no IP address assigned")
            return False
            
        # Display configuration
        print("3. Network Configuration:")
        print(f"   IP Address:  {config[0]}")
        print(f"   Subnet Mask: {config[1]}")
        print(f"   Gateway:     {config[2]}")
        print(f"   DNS Server:  {config[3]}")
        
        # Test connectivity
        print("4. Testing connectivity...")
        
        # Check if interface is connected
        if hasattr(lan, 'isconnected'):
            connected = lan.isconnected()
            print(f"   Link status: {'Connected' if connected else 'Disconnected'}")
        
        # Test socket creation
        try:
            test_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            test_socket.close()
            print("   Socket creation: OK")
        except Exception as e:
            print(f"   Socket creation: FAILED ({e})")
            return False
        
        # Memory check
        free_mem = gc.mem_free()
        print(f"5. Memory status:")
        print(f"   Free memory: {free_mem:,} bytes ({free_mem/(1024*1024):.1f}MB)")
        
        print("\n✓ Network setup successful!")
        print(f"✓ Board IP: {config[0]}")
        print(f"✓ Ready for file transfer testing")
        
        return config[0]
        
    except Exception as e:
        print(f"ERROR: Network setup failed: {e}")
        return False

def create_test_file(size_kb=100):
    """Create a small test file"""
    filename = f"test_{size_kb}kb.txt"
    print(f"\nCreating test file: {filename}")
    
    try:
        with open(filename, 'w') as f:
            # Write some test data
            for i in range(size_kb):
                f.write(f"Line {i:04d}: This is test data for network transfer testing.\n")
        
        # Verify file
        import os
        size = os.stat(filename)[6]
        print(f"✓ Created {filename} ({size} bytes)")
        return filename
        
    except Exception as e:
        print(f"ERROR creating test file: {e}")
        return None

def simple_http_test(ip_address):
    """Run a very simple HTTP server test"""
    print(f"\nStarting simple HTTP server on {ip_address}:8080...")
    
    try:
        # Create test file
        test_file = create_test_file(50)  # 50KB test file
        if not test_file:
            return False
        
        # Create server socket
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(('0.0.0.0', 8080))
        server.listen(1)
        
        print(f"✓ Server listening on http://{ip_address}:8080/")
        print("  Test with: curl http://{ip_address}:8080/ or open in browser")
        print("  Press Ctrl+C to stop")
        
        # Simple server loop
        request_count = 0
        while request_count < 5:  # Handle up to 5 requests
            try:
                client, addr = server.accept()
                print(f"\nConnection from {addr[0]}")
                
                # Read request
                request = client.recv(1024).decode('utf-8')
                print(f"Request: {request.split()[0:2] if request else 'Empty'}")
                
                # Send simple response
                response = f"""HTTP/1.1 200 OK\r
Content-Type: text/html\r
Connection: close\r
\r
<!DOCTYPE html>
<html>
<head><title>RA6M5 Network Test</title></head>
<body>
<h1>RA6M5 Network Test Successful!</h1>
<p>Board IP: {ip_address}</p>
<p>Free Memory: {gc.mem_free():,} bytes</p>
<p>Request #{request_count + 1}</p>
<p><a href="/download">Download test file</a></p>
</body>
</html>"""
                
                client.send(response.encode('utf-8'))
                client.close()
                
                request_count += 1
                print(f"✓ Served request #{request_count}")
                
            except KeyboardInterrupt:
                break
            except Exception as e:
                print(f"Request error: {e}")
        
        server.close()
        print(f"\n✓ HTTP test completed ({request_count} requests served)")
        return True
        
    except Exception as e:
        print(f"HTTP test failed: {e}")
        return False

def main():
    """Main test function"""
    # Test network setup
    ip_address = test_network_setup()
    if not ip_address:
        print("\nFAILED: Network setup unsuccessful")
        print("Check:")
        print("- Ethernet cable connection")
        print("- DHCP server availability")
        print("- Board Ethernet configuration")
        return
    
    # Run simple HTTP test
    print("\n" + "=" * 40)
    print("HTTP Server Test")
    print("=" * 40)
    
    if simple_http_test(ip_address):
        print("\n✓ All tests passed!")
        print(f"✓ Network ready for large file testing")
        print(f"✓ Run 'simple_web_server.py' for full file transfer tests")
    else:
        print("\n✗ HTTP test failed")

if __name__ == "__main__":
    main()
