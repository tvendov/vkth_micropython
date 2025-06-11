# RA6M5 Network Testing Suite

This collection of scripts tests the network capabilities and maximum file transfer sizes for the VK-RA6M5 board with MicroPython.

## Files Overview

1. **`network_setup.py`** - Quick network setup and basic connectivity test
2. **`memory_test.py`** - Memory and file system capability testing  
3. **`simple_web_server.py`** - Lightweight HTTP server for file transfers
4. **`network_test_main.py`** - Comprehensive testing suite

## Quick Start

### Step 1: Connect Hardware
- Connect Ethernet cable to the VK-RA6M5 board
- Ensure DHCP server is available on your network
- Connect to board via serial (COM4 as mentioned)

### Step 2: Basic Network Test
```python
# Run this first to verify network connectivity
exec(open('network_setup.py').read())
```

### Step 3: Memory Capabilities Test
```python
# Test memory and file creation limits
exec(open('memory_test.py').read())
```

### Step 4: File Transfer Testing
```python
# Start the web server for file transfer testing
exec(open('simple_web_server.py').read())
```

## Expected Results

### Network Configuration
- Board should obtain IP address via DHCP
- Typical configuration: 192.168.x.x/24
- Gateway and DNS should be automatically configured

### Memory Capabilities
Based on VK-RA6M5 specifications:
- **Internal RAM**: 512KB
- **OSPI RAM**: 8MB (external)
- **Expected max file size**: 4-8MB (limited by available memory)

### File Transfer Performance
- **Target speeds**: 1-10 MB/s (depending on network)
- **Maximum file size**: Up to 8MB (using OSPI RAM)
- **Concurrent connections**: 1-5 simultaneous downloads

## Testing Procedure

### 1. Network Setup Verification
```python
exec(open('network_setup.py').read())
```
**Expected output:**
```
RA6M5 Network Setup Test
========================================
1. Initializing LAN interface...
   LAN object created: <LAN>
   Interface activated
2. Waiting for DHCP configuration...
3. Network Configuration:
   IP Address:  192.168.1.100
   Subnet Mask: 255.255.255.0
   Gateway:     192.168.1.1
   DNS Server:  192.168.1.1
✓ Network setup successful!
✓ Board IP: 192.168.1.100
```

### 2. Memory Testing
```python
exec(open('memory_test.py').read())
```
**Expected capabilities:**
- RAM allocation: Up to 400KB
- File creation: Up to 8MB
- Read/write speeds: 1-5 MB/s

### 3. Web Server Testing
```python
exec(open('simple_web_server.py').read())
```

Then open browser to: `http://[BOARD_IP]:8080/`

**Test different file sizes:**
- 0.5MB - Should work reliably
- 1MB - Should work on most configurations  
- 2MB - Tests medium file capability
- 4MB - Tests large file capability
- 8MB - Maximum expected size

## Troubleshooting

### Network Issues
**Problem**: No IP address obtained
**Solutions:**
- Check Ethernet cable connection
- Verify DHCP server is running
- Try static IP configuration:
```python
lan = network.LAN()
lan.active(True)
lan.ifconfig(('192.168.1.100', '255.255.255.0', '192.168.1.1', '8.8.8.8'))
```

**Problem**: Connection timeouts
**Solutions:**
- Check firewall settings
- Verify network connectivity with ping
- Try different port (change WEB_SERVER_PORT)

### Memory Issues
**Problem**: Cannot create large files
**Solutions:**
- Run `gc.collect()` before testing
- Reduce file sizes
- Check available memory with `gc.mem_free()`

**Problem**: File creation fails
**Solutions:**
- Check file system space with `os.statvfs('/')`
- Remove existing files
- Try smaller chunk sizes

### Performance Issues
**Problem**: Slow transfer speeds
**Solutions:**
- Increase chunk sizes in server code
- Check network congestion
- Test with wired connection only

## Advanced Testing

### Custom File Sizes
```python
# Create custom size file (e.g., 3.5MB)
server = SimpleWebServer()
server.setup_network()
server.create_large_file(3.5, "custom_test.bin")
```

### Network Performance Testing
```python
# Use external tools for performance testing
# wget http://[BOARD_IP]:8080/create/4mb
# curl -o test.bin http://[BOARD_IP]:8080/create/2mb
```

### Multiple Concurrent Downloads
Open multiple browser tabs or use tools like:
```bash
# Terminal 1
wget http://192.168.1.100:8080/create/1mb -O test1.bin

# Terminal 2  
wget http://192.168.1.100:8080/create/1mb -O test2.bin
```

## Expected Performance Benchmarks

| File Size | Creation Time | Transfer Speed | Success Rate |
|-----------|---------------|----------------|--------------|
| 0.5MB     | <1s          | 5-10 MB/s     | 100%         |
| 1MB       | 1-2s         | 3-8 MB/s      | 95%+         |
| 2MB       | 2-4s         | 2-6 MB/s      | 90%+         |
| 4MB       | 4-8s         | 1-4 MB/s      | 80%+         |
| 8MB       | 8-15s        | 1-3 MB/s      | 70%+         |

## Hardware Specifications

**VK-RA6M5 Board:**
- MCU: RA6M5 (ARM Cortex-M33, 200MHz)
- Internal RAM: 512KB
- OSPI RAM: 8MB external
- Ethernet: 10/100 Mbps
- Flash: 2MB internal + 16MB QSPI

**Memory Map:**
- Internal RAM: 0x20000000 - 0x20080000 (512KB)
- OSPI RAM: 0x68000000 - 0x68800000 (8MB)
- QSPI Flash: 0x60000000 - 0x61000000 (16MB)

## Notes

- Files are created with unique patterns to prevent compression
- OSPI RAM provides additional space for large files
- Network performance depends on local network conditions
- Some file sizes may fail due to memory fragmentation
- Use Ctrl+C to stop servers gracefully
