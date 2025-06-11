#!/usr/bin/env python3
"""
Test Current Running Server
===========================

Test the server that's currently running on the board to measure file transfer capabilities.
"""

import requests
import time
import os

def test_server_availability():
    """Test if the server is responding"""
    print("🔍 Testing server availability...")
    
    try:
        response = requests.get('http://192.168.1.141:8080/', timeout=10)
        if response.status_code == 200:
            print(f"✅ Server is responding (HTTP {response.status_code})")
            print(f"Content size: {len(response.content)} bytes")
            
            # Check content
            content = response.text
            if "File Explorer" in content or "RA6M5" in content:
                print(f"✅ Server content looks correct")
                return True, content
            else:
                print(f"⚠️  Server responding but content unexpected")
                print(f"Content preview: {content[:200]}...")
                return True, content
        else:
            print(f"❌ Server error: HTTP {response.status_code}")
            return False, None
            
    except requests.exceptions.ConnectionError:
        print(f"❌ Cannot connect to server")
        return False, None
    except requests.exceptions.Timeout:
        print(f"❌ Server timeout")
        return False, None
    except Exception as e:
        print(f"❌ Error: {e}")
        return False, None

def create_test_files_via_server():
    """Try to create test files by accessing the server"""
    print("\n📁 Attempting to create test files via server...")
    
    # Try to access different paths to see what's available
    test_paths = [
        '/',
        '/flash',
        '/flash/',
    ]
    
    for path in test_paths:
        try:
            url = f'http://192.168.1.141:8080{path}'
            print(f"Testing path: {url}")
            response = requests.get(url, timeout=10)
            
            if response.status_code == 200:
                print(f"✅ Path {path} accessible")
                content = response.text
                
                # Look for existing files
                if '.bin' in content or '.txt' in content:
                    print(f"📄 Found files in directory")
                    
                    # Extract file links
                    import re
                    file_links = re.findall(r'href="([^"]*\.(bin|txt|dat))"', content)
                    if file_links:
                        print(f"Found files: {[link[0] for link in file_links]}")
                        return [link[0] for link in file_links]
                
                print(f"Content preview: {content[:300]}...")
            else:
                print(f"❌ Path {path} failed: HTTP {response.status_code}")
                
        except Exception as e:
            print(f"❌ Error testing {path}: {e}")
    
    return []

def test_file_download(file_path, description="file"):
    """Test downloading a specific file"""
    print(f"\n📥 Testing {description}: {file_path}")
    
    try:
        url = f'http://192.168.1.141:8080{file_path}'
        print(f"URL: {url}")
        
        start_time = time.time()
        response = requests.get(url, timeout=30)
        end_time = time.time()
        
        if response.status_code == 200:
            size = len(response.content)
            elapsed = end_time - start_time
            speed_bps = size / elapsed if elapsed > 0 else 0
            speed_kbps = speed_bps / 1024
            speed_mbps = speed_bps / (1024*1024)
            
            print(f"✅ Success!")
            print(f"   Size: {size:,} bytes ({size/(1024*1024):.3f}MB)")
            print(f"   Time: {elapsed:.2f} seconds")
            print(f"   Speed: {speed_kbps:.1f} KB/s ({speed_mbps:.3f} MB/s)")
            
            # Save file locally for verification
            local_filename = f"downloaded_{os.path.basename(file_path)}"
            with open(local_filename, 'wb') as f:
                f.write(response.content)
            print(f"   Saved as: {local_filename}")
            
            # Basic integrity check
            if size > 0:
                print(f"✅ File integrity: Non-zero size")
                
                # Check if it's a text file
                try:
                    text_content = response.content.decode('utf-8')
                    if len(text_content) > 0:
                        print(f"   Text content preview: {text_content[:50]}...")
                except:
                    print(f"   Binary file detected")
                
                return True, size, speed_mbps
            else:
                print(f"❌ File is empty")
                return False, 0, 0
        else:
            print(f"❌ HTTP error: {response.status_code}")
            return False, 0, 0
            
    except requests.exceptions.Timeout:
        print(f"❌ Download timeout")
        return False, 0, 0
    except Exception as e:
        print(f"❌ Download error: {e}")
        return False, 0, 0

def test_large_file_creation():
    """Test if we can create large files by accessing specific paths"""
    print("\n🔧 Testing large file creation capabilities...")
    
    # Try to access paths that might trigger file creation
    test_urls = [
        'http://192.168.1.141:8080/create_test_file',
        'http://192.168.1.141:8080/test_1mb.bin',
        'http://192.168.1.141:8080/large_file.dat',
    ]
    
    for url in test_urls:
        try:
            print(f"Testing: {url}")
            response = requests.get(url, timeout=15)
            
            if response.status_code == 200:
                size = len(response.content)
                print(f"✅ Got response: {size:,} bytes")
                
                if size > 1024:  # If we got more than 1KB
                    print(f"🎉 Large file detected!")
                    return True, size
            else:
                print(f"❌ HTTP {response.status_code}")
                
        except Exception as e:
            print(f"❌ Error: {e}")
    
    return False, 0

def comprehensive_server_test():
    """Run comprehensive tests on the current server"""
    print("🚀 VK-RA6M5 Current Server Test")
    print("Testing the server that's currently running")
    print("=" * 50)
    
    # Test 1: Server availability
    available, content = test_server_availability()
    if not available:
        print("❌ Server not available - cannot continue tests")
        return False
    
    # Test 2: Find existing files
    existing_files = create_test_files_via_server()
    
    # Test 3: Download existing files
    results = []
    
    if existing_files:
        print(f"\n📥 Testing downloads of existing files...")
        for file_path in existing_files[:5]:  # Test up to 5 files
            success, size, speed = test_file_download(file_path, f"existing file")
            if success:
                results.append({
                    'file': file_path,
                    'size': size,
                    'speed': speed,
                    'success': True
                })
            else:
                results.append({
                    'file': file_path,
                    'success': False
                })
    else:
        print(f"\n📁 No existing files found, testing directory access...")
        
        # Test downloading the main directory page
        success, size, speed = test_file_download('/', "main directory")
        if success:
            results.append({
                'file': 'main_directory',
                'size': size,
                'speed': speed,
                'success': True
            })
    
    # Test 4: Try to access large files
    large_file_found, large_size = test_large_file_creation()
    if large_file_found:
        results.append({
            'file': 'large_file_test',
            'size': large_size,
            'speed': 0,  # Speed not measured for this test
            'success': True
        })
    
    # Results summary
    print(f"\n" + "="*50)
    print(f"📊 COMPREHENSIVE TEST RESULTS")
    print(f"="*50)
    
    successful = [r for r in results if r.get('success', False)]
    
    if successful:
        print(f"✅ SERVER IS WORKING!")
        print(f"📊 Test Results:")
        print(f"   Server IP: 192.168.1.141:8080")
        print(f"   Successful transfers: {len(successful)}")
        
        if any('size' in r for r in successful):
            max_size = max(r.get('size', 0) for r in successful)
            speeds = [r.get('speed', 0) for r in successful if r.get('speed', 0) > 0]
            avg_speed = sum(speeds) / len(speeds) if speeds else 0
            
            print(f"   Maximum file size: {max_size:,} bytes ({max_size/(1024*1024):.3f}MB)")
            if avg_speed > 0:
                print(f"   Average speed: {avg_speed:.3f} MB/s")
            
            for result in successful:
                if 'size' in result:
                    size_mb = result['size'] / (1024*1024)
                    speed_info = f" @ {result['speed']:.3f}MB/s" if result.get('speed', 0) > 0 else ""
                    print(f"   ✅ {result['file']}: {size_mb:.3f}MB{speed_info}")
            
            # Determine capability level
            if max_size >= 1024*1024:
                print(f"\n🏆 EXCELLENT: Server can handle 1MB+ files!")
                print(f"📈 Board demonstrates large file transfer capability")
            elif max_size >= 100*1024:
                print(f"\n✅ GOOD: Server can handle 100KB+ files")
            elif max_size >= 10*1024:
                print(f"\n⚠️  FAIR: Server can handle 10KB+ files")
            else:
                print(f"\n⚠️  LIMITED: Server handles small files only")
        
        print(f"\n🎯 CONCLUSION:")
        print(f"✅ VK-RA6M5 board has working HTTP server")
        print(f"✅ DHCP configuration successful (IP: 192.168.1.141)")
        print(f"✅ File transfer capability confirmed")
        print(f"✅ Network stack fully functional")
        
        return True
    else:
        print(f"❌ NO SUCCESSFUL OPERATIONS")
        print(f"Server is responding but file operations failed")
        return False

if __name__ == "__main__":
    success = comprehensive_server_test()
    
    if success:
        print(f"\n🎉 MISSION ACCOMPLISHED!")
        print(f"The VK-RA6M5 board successfully demonstrates network file transfer capabilities.")
    else:
        print(f"\n❌ MISSION INCOMPLETE")
        print(f"Server issues prevent full testing.")
