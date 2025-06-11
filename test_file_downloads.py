#!/usr/bin/env python3
"""
Test File Downloads from VK-RA6M5 Board
=======================================

Test downloading files from the board's web server to measure
maximum transfer speeds and validate file sizes.
"""

import requests
import time
import os

def test_download(url, filename, expected_size):
    """Download a file and measure performance"""
    print(f"\n📥 Testing download: {filename}")
    print(f"   URL: {url}")
    print(f"   Expected size: {expected_size:,} bytes ({expected_size/(1024*1024):.1f}MB)")
    
    try:
        start_time = time.time()
        
        # Download with streaming to handle large files
        response = requests.get(url, stream=True, timeout=30)
        response.raise_for_status()
        
        # Get content length from headers
        content_length = response.headers.get('content-length')
        if content_length:
            reported_size = int(content_length)
            print(f"   Server reports: {reported_size:,} bytes")
        else:
            reported_size = None
        
        # Download and save file
        downloaded = 0
        chunk_size = 8192
        
        with open(filename, 'wb') as f:
            for chunk in response.iter_content(chunk_size=chunk_size):
                if chunk:
                    f.write(chunk)
                    downloaded += len(chunk)
                    
                    # Progress for large files
                    if expected_size > 512*1024 and downloaded % (256*1024) == 0:
                        elapsed = time.time() - start_time
                        if elapsed > 0:
                            speed = (downloaded / elapsed) / (1024*1024)  # MB/s
                            progress = (downloaded / expected_size) * 100
                            print(f"   Progress: {downloaded//1024}KB ({progress:.1f}%) - {speed:.2f}MB/s")
        
        end_time = time.time()
        elapsed = end_time - start_time
        
        # Verify download
        actual_size = os.path.getsize(filename)
        
        if actual_size == expected_size:
            speed_mbps = (actual_size / elapsed) / (1024*1024) if elapsed > 0 else 0
            speed_kbps = (actual_size / elapsed) / 1024 if elapsed > 0 else 0
            
            print(f"   ✅ SUCCESS!")
            print(f"   Downloaded: {actual_size:,} bytes")
            print(f"   Time: {elapsed:.2f} seconds")
            print(f"   Speed: {speed_mbps:.2f} MB/s ({speed_kbps:.1f} KB/s)")
            
            # Clean up
            os.remove(filename)
            
            return True, speed_mbps
        else:
            print(f"   ❌ SIZE MISMATCH!")
            print(f"   Expected: {expected_size:,} bytes")
            print(f"   Downloaded: {actual_size:,} bytes")
            return False, 0
            
    except requests.exceptions.Timeout:
        print(f"   ❌ TIMEOUT - Download took too long")
        return False, 0
    except requests.exceptions.ConnectionError:
        print(f"   ❌ CONNECTION ERROR - Cannot reach server")
        return False, 0
    except Exception as e:
        print(f"   ❌ ERROR: {e}")
        return False, 0

def test_all_files():
    """Test downloading all available files"""
    board_ip = "192.168.1.141"
    base_url = f"http://{board_ip}:8080"
    
    print("🚀 VK-RA6M5 File Transfer Performance Test")
    print("=" * 50)
    print(f"Board IP: {board_ip}")
    print(f"Server URL: {base_url}")
    
    # Test files to download (size in bytes)
    test_files = [
        ("test_0.5mb.bin", 0.5 * 1024 * 1024),
        ("test_1mb.bin", 1 * 1024 * 1024),
        ("test_2mb.bin", 2 * 1024 * 1024),
        ("test_4mb.bin", 4 * 1024 * 1024),
    ]
    
    results = []
    max_successful_size = 0
    total_tests = len(test_files)
    successful_tests = 0
    
    for filename, expected_size in test_files:
        url = f"{base_url}/{filename}"
        success, speed = test_download(url, filename, int(expected_size))
        
        results.append({
            'filename': filename,
            'size_mb': expected_size / (1024*1024),
            'success': success,
            'speed_mbps': speed
        })
        
        if success:
            successful_tests += 1
            max_successful_size = expected_size / (1024*1024)
        else:
            break  # Stop on first failure
    
    # Print summary
    print("\n" + "=" * 50)
    print("📊 TEST RESULTS SUMMARY")
    print("=" * 50)
    
    for result in results:
        status = "✅ PASS" if result['success'] else "❌ FAIL"
        print(f"{result['filename']:15} ({result['size_mb']:4.1f}MB): {status}")
        if result['success']:
            print(f"                    Speed: {result['speed_mbps']:.2f} MB/s")
    
    print(f"\n🎯 FINAL RESULTS:")
    print(f"   Tests passed: {successful_tests}/{total_tests}")
    print(f"   Maximum file size: {max_successful_size:.1f}MB")
    
    if successful_tests > 0:
        avg_speed = sum(r['speed_mbps'] for r in results if r['success']) / successful_tests
        print(f"   Average speed: {avg_speed:.2f} MB/s")
        
        # Determine performance category
        if max_successful_size >= 4:
            print(f"   🏆 EXCELLENT: Board can handle large files (4MB+)")
        elif max_successful_size >= 2:
            print(f"   ✅ GOOD: Board can handle medium files (2MB+)")
        elif max_successful_size >= 1:
            print(f"   ⚠️  FAIR: Board can handle small files (1MB+)")
        else:
            print(f"   ❌ LIMITED: Board has limited file transfer capability")
    
    return max_successful_size, successful_tests

def test_server_availability():
    """Test if the server is responding"""
    board_ip = "192.168.1.141"
    url = f"http://{board_ip}:8080/"
    
    print(f"🔍 Testing server availability...")
    print(f"   URL: {url}")
    
    try:
        response = requests.get(url, timeout=10)
        if response.status_code == 200:
            print(f"   ✅ Server is responding (HTTP {response.status_code})")
            
            # Check if it contains expected content
            content = response.text
            if "RA6M5" in content and "Test Files" in content:
                print(f"   ✅ Server content looks correct")
                return True
            else:
                print(f"   ⚠️  Server responding but content unexpected")
                return False
        else:
            print(f"   ❌ Server error: HTTP {response.status_code}")
            return False
            
    except requests.exceptions.ConnectionError:
        print(f"   ❌ Cannot connect to server")
        return False
    except requests.exceptions.Timeout:
        print(f"   ❌ Server timeout")
        return False
    except Exception as e:
        print(f"   ❌ Error: {e}")
        return False

if __name__ == "__main__":
    # Check if requests library is available
    try:
        import requests
    except ImportError:
        print("❌ Error: requests library not installed")
        print("Install with: pip install requests")
        exit(1)
    
    # Test server availability first
    if test_server_availability():
        print("\n" + "=" * 50)
        
        # Run file transfer tests
        max_size, successful = test_all_files()
        
        print(f"\n🎉 TESTING COMPLETED!")
        print(f"Maximum file transfer size achieved: {max_size:.1f}MB")
        
        if max_size >= 4:
            print(f"🏆 EXCELLENT PERFORMANCE - Board exceeds expectations!")
        elif max_size >= 2:
            print(f"✅ GOOD PERFORMANCE - Board meets requirements!")
        else:
            print(f"⚠️  LIMITED PERFORMANCE - Board has constraints")
            
    else:
        print(f"\n❌ Cannot test file transfers - server not available")
        print(f"Make sure:")
        print(f"- Board web server is running")
        print(f"- Network connection is working")
        print(f"- IP address 192.168.1.141 is correct")
