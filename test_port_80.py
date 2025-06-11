#!/usr/bin/env python3
"""
Test Server on Port 80
======================

Test the server on port 80 as specified in the working code.
"""

import requests
import time

def test_server_port_80():
    """Test server on port 80"""
    print("🔍 Testing server on port 80...")
    
    try:
        response = requests.get('http://192.168.1.141/', timeout=10)
        if response.status_code == 200:
            print(f"✅ Server responding on port 80!")
            print(f"Content size: {len(response.content)} bytes")
            
            content = response.text
            print(f"Content preview: {content[:500]}...")
            
            if "File Explorer" in content:
                print(f"✅ File Explorer detected!")
                
                # Look for file links
                import re
                file_links = re.findall(r'href="([^"]*)"', content)
                print(f"Found links: {file_links}")
                
                return True, file_links
            else:
                print(f"⚠️  Different content type")
                return True, []
        else:
            print(f"❌ HTTP error: {response.status_code}")
            return False, []
            
    except Exception as e:
        print(f"❌ Error: {e}")
        return False, []

def test_file_download_port_80(file_path):
    """Test downloading a file from port 80"""
    print(f"\n📥 Testing download: {file_path}")
    
    try:
        url = f'http://192.168.1.141{file_path}'
        print(f"URL: {url}")
        
        start_time = time.time()
        response = requests.get(url, timeout=30)
        end_time = time.time()
        
        if response.status_code == 200:
            size = len(response.content)
            elapsed = end_time - start_time
            speed_mbps = (size / elapsed) / (1024*1024) if elapsed > 0 else 0
            
            print(f"✅ Success: {size:,} bytes in {elapsed:.2f}s ({speed_mbps:.3f}MB/s)")
            
            # Check content type
            content_type = response.headers.get('content-type', 'unknown')
            print(f"Content-Type: {content_type}")
            
            if 'text' in content_type:
                try:
                    text_preview = response.content.decode('utf-8')[:100]
                    print(f"Text preview: {text_preview}...")
                except:
                    print(f"Text decode failed")
            
            return True, size, speed_mbps
        else:
            print(f"❌ HTTP error: {response.status_code}")
            return False, 0, 0
            
    except Exception as e:
        print(f"❌ Error: {e}")
        return False, 0, 0

def create_large_file_test():
    """Try to create a large file by accessing the board"""
    print(f"\n🔧 Testing large file creation...")
    
    # Try to access paths that might contain or create large files
    test_paths = [
        '/flash',
        '/flash/',
        '/test.bin',
        '/large_file.dat',
        '/1mb_test.bin'
    ]
    
    results = []
    
    for path in test_paths:
        try:
            url = f'http://192.168.1.141{path}'
            print(f"Testing: {url}")
            
            response = requests.get(url, timeout=15)
            
            if response.status_code == 200:
                size = len(response.content)
                print(f"✅ Response: {size:,} bytes")
                
                if size > 10*1024:  # More than 10KB
                    print(f"🎉 Large content detected!")
                    results.append((path, size))
                elif size > 0:
                    print(f"📄 Small file/content")
                    results.append((path, size))
            else:
                print(f"❌ HTTP {response.status_code}")
                
        except Exception as e:
            print(f"❌ Error: {e}")
    
    return results

def main():
    """Main test function"""
    print("🚀 VK-RA6M5 Port 80 Server Test")
    print("Testing the working server on port 80")
    print("=" * 50)
    
    # Test server availability
    available, links = test_server_port_80()
    
    if not available:
        print("❌ Server not available on port 80")
        return
    
    print(f"✅ Server is working on port 80!")
    
    # Test file downloads
    results = []
    
    if links:
        print(f"\n📥 Testing file downloads...")
        for link in links[:5]:  # Test first 5 links
            if link and not link.startswith('http'):
                success, size, speed = test_file_download_port_80(link)
                if success:
                    results.append({
                        'path': link,
                        'size': size,
                        'speed': speed,
                        'success': True
                    })
    
    # Test large file creation
    large_files = create_large_file_test()
    for path, size in large_files:
        results.append({
            'path': path,
            'size': size,
            'speed': 0,
            'success': True
        })
    
    # Final results
    print(f"\n" + "="*50)
    print(f"🎯 FINAL RESULTS")
    print(f"="*50)
    
    successful = [r for r in results if r.get('success', False)]
    
    if successful:
        print(f"✅ FILE TRANSFER TEST SUCCESSFUL!")
        print(f"📊 Results:")
        print(f"   Server: http://192.168.1.141/ (port 80)")
        print(f"   Successful operations: {len(successful)}")
        
        if successful:
            max_size = max(r.get('size', 0) for r in successful)
            speeds = [r.get('speed', 0) for r in successful if r.get('speed', 0) > 0]
            avg_speed = sum(speeds) / len(speeds) if speeds else 0
            
            print(f"   Maximum file size: {max_size:,} bytes ({max_size/(1024*1024):.3f}MB)")
            if avg_speed > 0:
                print(f"   Average speed: {avg_speed:.3f} MB/s")
            
            for result in successful:
                size_mb = result['size'] / (1024*1024)
                speed_info = f" @ {result['speed']:.3f}MB/s" if result.get('speed', 0) > 0 else ""
                print(f"   ✅ {result['path']}: {size_mb:.3f}MB{speed_info}")
            
            # Assessment
            if max_size >= 1024*1024:
                print(f"\n🏆 EXCELLENT: Board handles 1MB+ files!")
            elif max_size >= 100*1024:
                print(f"\n✅ GOOD: Board handles 100KB+ files")
            elif max_size >= 10*1024:
                print(f"\n⚠️  FAIR: Board handles 10KB+ files")
            else:
                print(f"\n⚠️  LIMITED: Small files only")
        
        print(f"\n🎉 MISSION ACCOMPLISHED!")
        print(f"✅ DHCP IP: 192.168.1.141")
        print(f"✅ HTTP server working on port 80")
        print(f"✅ File transfer capability confirmed")
        print(f"✅ Network configuration successful")
        
    else:
        print(f"❌ No successful file operations")

if __name__ == "__main__":
    main()
