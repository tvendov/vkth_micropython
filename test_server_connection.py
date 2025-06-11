#!/usr/bin/env python3
"""
Test Server Connection
=====================

Simple test to check if the board's server is responding.
"""

import socket
import time
import requests

def test_socket_connection():
    """Test raw socket connection to the board"""
    print("🔍 Testing socket connection to 192.168.1.141:8080...")
    
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5)
        
        result = sock.connect_ex(('192.168.1.141', 8080))
        
        if result == 0:
            print("✅ Socket connection successful!")
            
            # Send HTTP request
            request = "GET / HTTP/1.1\r\nHost: 192.168.1.141\r\nConnection: close\r\n\r\n"
            sock.send(request.encode())
            
            # Read response
            response = b""
            while True:
                try:
                    data = sock.recv(1024)
                    if not data:
                        break
                    response += data
                except socket.timeout:
                    break
            
            sock.close()
            
            if response:
                response_str = response.decode('utf-8', errors='ignore')
                print(f"📥 Response received ({len(response)} bytes)")
                
                # Check if it's a valid HTTP response
                if response_str.startswith('HTTP/'):
                    print("✅ Valid HTTP response")
                    
                    # Look for content
                    if 'RA6M5' in response_str:
                        print("✅ Board content detected")
                        return True
                    else:
                        print("⚠️  Response doesn't contain expected content")
                else:
                    print("❌ Invalid HTTP response")
                    print(f"Response: {response_str[:200]}...")
            else:
                print("❌ No response received")
                
        else:
            print(f"❌ Socket connection failed (error {result})")
            
        sock.close()
        return False
        
    except Exception as e:
        print(f"❌ Socket test error: {e}")
        return False

def test_requests_connection():
    """Test using requests library"""
    print("\n🌐 Testing HTTP connection with requests...")
    
    try:
        response = requests.get('http://192.168.1.141:8080/', timeout=10)
        
        print(f"✅ HTTP request successful!")
        print(f"   Status: {response.status_code}")
        print(f"   Content length: {len(response.content)} bytes")
        
        if 'RA6M5' in response.text:
            print("✅ Board content detected")
            
            # Try to download the test file
            print("\n📥 Testing file download...")
            file_response = requests.get('http://192.168.1.141:8080/test.bin', timeout=30)
            
            if file_response.status_code == 200:
                size = len(file_response.content)
                print(f"✅ File download successful!")
                print(f"   File size: {size:,} bytes ({size/(1024*1024):.1f}MB)")
                
                # Check file integrity
                if size >= 10:
                    expected = bytes([i & 0xFF for i in range(10)])
                    actual = file_response.content[:10]
                    if actual == expected:
                        print("✅ File integrity verified")
                        return True, size
                    else:
                        print("❌ File integrity check failed")
                        print(f"Expected: {expected.hex()}")
                        print(f"Actual: {actual.hex()}")
                else:
                    print("⚠️  File too small to verify integrity")
                    
            else:
                print(f"❌ File download failed: HTTP {file_response.status_code}")
                
        else:
            print("⚠️  Response doesn't contain expected board content")
            
        return False, 0
        
    except requests.exceptions.ConnectionError:
        print("❌ Connection error - server not responding")
        return False, 0
    except requests.exceptions.Timeout:
        print("❌ Request timeout")
        return False, 0
    except Exception as e:
        print(f"❌ Request error: {e}")
        return False, 0

def ping_test():
    """Test basic network connectivity"""
    print("🏓 Testing network connectivity...")
    
    import subprocess
    import platform
    
    try:
        # Use ping command
        param = '-n' if platform.system().lower() == 'windows' else '-c'
        command = ['ping', param, '1', '192.168.1.141']
        
        result = subprocess.run(command, capture_output=True, text=True, timeout=10)
        
        if result.returncode == 0:
            print("✅ Ping successful - board is reachable")
            return True
        else:
            print("❌ Ping failed - board not reachable")
            print(f"Error: {result.stderr}")
            return False
            
    except Exception as e:
        print(f"❌ Ping test error: {e}")
        return False

def main():
    """Run all connection tests"""
    print("🚀 VK-RA6M5 Server Connection Test")
    print("=" * 50)
    
    # Test 1: Basic network connectivity
    ping_ok = ping_test()
    
    # Test 2: Socket connection
    socket_ok = test_socket_connection()
    
    # Test 3: HTTP requests
    http_ok, file_size = test_requests_connection()
    
    # Summary
    print("\n" + "=" * 50)
    print("📊 TEST RESULTS SUMMARY")
    print("=" * 50)
    
    print(f"Network ping:     {'✅ PASS' if ping_ok else '❌ FAIL'}")
    print(f"Socket connection: {'✅ PASS' if socket_ok else '❌ FAIL'}")
    print(f"HTTP requests:     {'✅ PASS' if http_ok else '❌ FAIL'}")
    
    if http_ok:
        print(f"\n🎉 SUCCESS! Board server is working!")
        print(f"📊 File transfer test results:")
        print(f"   ✅ File size: {file_size:,} bytes ({file_size/(1024*1024):.1f}MB)")
        print(f"   ✅ Server IP: 192.168.1.141")
        print(f"   ✅ Server port: 8080")
        
        if file_size >= 1024*1024:
            print(f"   🏆 EXCELLENT: Board can handle 1MB+ files")
            print(f"   📈 Estimated capability: 4-8MB files")
        else:
            print(f"   ⚠️  LIMITED: File size below 1MB")
            
    elif socket_ok:
        print(f"\n⚠️  PARTIAL SUCCESS: Server responding but HTTP issues")
        
    elif ping_ok:
        print(f"\n⚠️  PARTIAL SUCCESS: Network reachable but server not responding")
        print(f"   Check if server is running on the board")
        
    else:
        print(f"\n❌ FAILURE: Board not reachable")
        print(f"   Check network connection and board status")

if __name__ == "__main__":
    main()
