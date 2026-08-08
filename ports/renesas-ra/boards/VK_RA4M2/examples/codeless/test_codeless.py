"""
* Copyright (C) 2018 Dialog Semiconductor.
 * This computer program includes Confidential, Proprietary Information
 * of Dialog Semiconductor. All Rights Reserved.
 """
 
from codeless import *
import time

remote_peer = codeless(CodelessLink.BLE, 'EB:EF:0A:6A:27:9B')  # D3:30:D9:FA:1D:C2 'EB:EF:0A:6A:27:9B' 'D5:59:63:DA:9A:13'
status = remote_peer.connect()

try:
    status, reply_string = remote_peer.send_command("ATr+IOCFG=10,4", 2)
    print(reply_string)

    status, reply_string = remote_peer.send_command("ATr+IO=10,1", 2)
    print(reply_string)

    time.sleep(1)

    status, reply_string = remote_peer.send_command("ATr+IO=10,0", 2)
    print(reply_string)

    status, reply_string = remote_peer.send_command("ATr+PRINT=Goodnight!", 2)
    print(reply_string)

    status, reply_string = remote_peer.send_command("ATr+PRIsT=Goodnight!", 2)
    print(reply_string)

    status, reply_string = remote_peer.send_command("ATr+IOCFG", 5)
    print(reply_string)

    status, reply_string = remote_peer.send_command("ATr+BDADDR", 5)
    print(reply_string)

    remote_peer.stop()

except:
    remote_peer.stop()

status = load_bin_to_ram('COM22', 'codeless_585.bin')

local_peer = codeless(CodelessLink.UART, 'COM22') # /dev/ttyACM0
status = local_peer.connect()

try:
    status, reply_string = local_peer.send_command("AT", 4)
    print(reply_string)

    status, reply_string = local_peer.send_command("ATE=0", 2)
    print(reply_string)

    status, reply_string = local_peer.send_command("ATI", 4)
    print(reply_string)

    status, reply_string = local_peer.send_command("AT+IOCFG=10,4", 2)
    print(reply_string)

    status, reply_string = local_peer.send_command("AT+IO=10,1", 2)
    print(reply_string)

    time.sleep(1)

    status, reply_string = local_peer.send_command("AT+IO=10,0", 2)
    print(reply_string)

    status, reply_string = local_peer.send_command("AT+PRINT=Goodnight!", 2)
    print(reply_string)

    status, reply_string = local_peer.send_command("AT+PRIsT=Goodnight!", 2)
    print(reply_string)

    status, reply_string = local_peer.send_command("AT+IOCFG", 3)
    print(reply_string)

    status, reply_string = local_peer.send_command("AT+ADVSTOP", 3)
    print(reply_string)

    status, reply_string = local_peer.send_command("AT+CENTRAL", 3)
    print(reply_string)

    status, reply_string = local_peer.send_command("AT+GAPSCAN", 10)
    print(reply_string)

    local_peer.stop()

except:
    local_peer.stop()
