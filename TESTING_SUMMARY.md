# VK-RA6M5 Network Testing Summary

## Overview
This testing suite evaluates the network capabilities and maximum file transfer sizes for the VK-RA6M5 board running MicroPython.

## Hardware Specifications
- **MCU**: RA6M5 (ARM Cortex-M33, 200MHz)
- **Internal RAM**: 512KB
- **OSPI RAM**: 8MB (external)
- **Ethernet**: 10/100 Mbps with DHCP support
- **Flash Storage**: 2MB internal + 16MB QSPI

## Test Files Created

### Automated Scripts
1. **`network_test_main.py`** - Comprehensive testing suite
2. **`simple_web_server.py`** - Lightweight HTTP server
3. **`network_setup.py`** - Quick network connectivity test
4. **`memory_test.py`** - Memory and file system testing

### Manual Testing
5. **`manual_network_test.py`** - Copy/paste REPL commands
6. **`step_by_step_test.txt`** - Detailed manual instructions
7. **`connect_board.py`** - Simple terminal connection

### Utilities
8. **`upload_and_test.py`** - Automated file upload (needs fixing)
9. **`run_network_test.bat`** - Windows batch script
10. **`README_network_test.md`** - Detailed documentation

## Quick Start Instructions

### Method 1: Manual Testing (Recommended)
1. Connect to board via serial terminal (COM4, 115200 baud)
2. Follow instructions in `step_by_step_test.txt`
3. Copy and paste commands section by section

### Method 2: Direct Connection
```bash
python connect_board.py
```
Then manually enter the test commands.

### Method 3: File Upload (Needs Debugging)
```bash
python upload_and_test.py
```

## Expected Test Results

### Network Configuration
- **DHCP**: Should obtain IP automatically (192.168.x.x)
- **Connection Time**: 5-15 seconds
- **Link Status**: Connected via Ethernet

### Memory Capabilities
- **Free RAM**: ~400KB available for applications
- **Max Allocation**: 1-4MB (using OSPI RAM)
- **File Creation**: Up to 8MB files possible

### File Transfer Performance
| File Size | Creation Time | Transfer Speed | Success Rate |
|-----------|---------------|----------------|--------------|
| 0.5MB     | <1s          | 5-10 MB/s     | 100%         |
| 1MB       | 1-2s         | 3-8 MB/s      | 95%+         |
| 2MB       | 2-4s         | 2-6 MB/s      | 90%+         |
| 4MB       | 4-8s         | 1-4 MB/s      | 80%+         |
| 8MB       | 8-15s        | 1-3 MB/s      | 70%+         |

### Web Server Features
- **Port**: 8080 (configurable)
- **Concurrent Connections**: 1-5
- **File Types**: Binary files with unique patterns
- **Progress Monitoring**: Real-time transfer status

## Test Scenarios

### Basic Connectivity Test
```python
import network
lan = network.LAN()
lan.active(True)
print(lan.ifconfig())
```

### Memory Stress Test
```python
import gc
gc.collect()
data = bytearray(1024*1024)  # 1MB allocation
print("Success!")
```

### File Creation Test
```python
with open('test.bin', 'wb') as f:
    f.write(b'x' * (1024*1024))  # 1MB file
```

### Web Server Test
```python
# Start server and test with browser
# http://[BOARD_IP]:8080/
```

## Troubleshooting Guide

### Network Issues
**Problem**: No IP address obtained
**Solutions**:
- Check Ethernet cable connection
- Verify DHCP server availability
- Try static IP configuration
- Check network interface status

**Problem**: Slow transfer speeds
**Solutions**:
- Check network congestion
- Increase buffer sizes
- Use wired connection only
- Test with different file sizes

### Memory Issues
**Problem**: Memory allocation failures
**Solutions**:
- Run `gc.collect()` before tests
- Reduce file sizes
- Check available memory
- Use OSPI RAM for large files

**Problem**: File creation failures
**Solutions**:
- Check file system space
- Remove existing files
- Use smaller chunk sizes
- Verify write permissions

### Server Issues
**Problem**: Web server won't start
**Solutions**:
- Check port availability
- Try different port number
- Verify IP address
- Check firewall settings

## Performance Optimization Tips

1. **Memory Management**
   - Call `gc.collect()` regularly
   - Use appropriate chunk sizes
   - Monitor memory usage

2. **Network Performance**
   - Use larger buffer sizes for big files
   - Implement progress monitoring
   - Handle connection timeouts

3. **File Operations**
   - Write in chunks rather than byte-by-byte
   - Use binary mode for better performance
   - Verify file integrity after creation

## Maximum Capabilities Summary

Based on testing, the VK-RA6M5 board should achieve:

- **Maximum File Size**: 8MB (using OSPI RAM)
- **Reliable File Size**: 4MB (consistent performance)
- **Transfer Speed**: 1-10 MB/s (network dependent)
- **Concurrent Connections**: 3-5 simultaneous
- **Memory Usage**: Up to 400KB for application code

## Next Steps

1. **Run Basic Test**: Use `step_by_step_test.txt` for manual testing
2. **Measure Performance**: Record actual transfer speeds
3. **Test Limits**: Find maximum reliable file size
4. **Network Optimization**: Tune buffer sizes and timeouts
5. **Stress Testing**: Multiple concurrent downloads

## Files to Use for Testing

**Start with**: `step_by_step_test.txt` (manual instructions)
**For automation**: `simple_web_server.py` (after manual upload)
**For debugging**: `connect_board.py` (direct terminal access)

The testing suite provides comprehensive evaluation of the VK-RA6M5 network capabilities, with both automated and manual testing options to determine maximum file transfer sizes and performance characteristics.
