"""
* Copyright (C) <Year> Dialog Semiconductor.
 * This computer program includes Confidential, Proprietary Information
 * of Dialog Semiconductor. All Rights Reserved.
 """
#!/usr/bin/env python

############################### codeless.py ###############################

import serial
import time
import sys
import signal
import struct
import threading
import random


#Default console width
WIDTH = 80


###############################
#Global definitions
TEST_001_LOOPS = 3
TEST_002_LOOPS = 2


GAPSTATUS_PERIPHERAL = 0
GAPSTATUS_CENTRAL = 1
GAPSTATUS_DISCONNECTED = 0
GAPSTATUS_CONNECTED = 1

#DBG = True
DBG = False

dummy_text = "But I must explain to you how all this mistaken idea of denouncing pleasure and praising pain was born and I will give you a complete account of the system, and expound the actual teachings of the great explorer of the truth, the master-builder of human happiness. No one rejects, dislikes, or avoids pleasure itself, because it is pleasure, but because those who do not know how to pursue pleasure rationally encounter consequences that are extremely painful. Nor again is there anyone who loves or pursues or desires to obtain pain of itself, because it is pain, but because occasionally circumstances occur in which toil and pain can procure him some great pleasure. To take a trivial example, which of us ever undertakes laborious physical exercise, except to obtain some advantage from it? But who has any right to find fault with a man who chooses to enjoy a pleasure that has no annoying consequences, or one who avoids a pain that produces no resultant pleasure? On the other hand, we denounce with righteous indignation and dislike men who are so beguiled and demoralized by the charms of pleasure of the moment, so blinded by desire, that they cannot foresee"


def CodelessError(error):
    raise Exception(error)


def printdbg(s):
    if(DBG):
        print (s)



def print_test(str):
    txt = ('\n' + '+' + '=' * (WIDTH - 2) + '+' + \
           '\n|' + ' ' * ((WIDTH - 2 - len(str)) / 2) + str + ' ' * ((WIDTH + 1 - 2 - len(str)) / 2) + '|' + \
           '\n' + '+' + '=' * (WIDTH - 2) + '+')
    print(txt)



def AT_Write(ser, data):
    print ("-->" + data)
    data=data+"\r"
    #print(data)
    ser.write(data)
    #time.sleep(0.5)


def AT_Read(ser, timeout=0, nochk = False):

    retval = ""
    #print("AT_Read")
    echo = 1
    if echo == 1:
        printdbg("ECHO")
        output = ''
        data=''
        t1=float(time.time())
        while True:
            data = ser.read(1)
            printdbg(data)
            output += data
            t2=float(time.time())
            if timeout==0:
                if data == '\n' :
                    printdbg("CR")
                    break
            else:
                if data == '\n' or t2-t1>timeout:
                    break
        printdbg("READ ECHO:" + output)
        if output == "OK":
            return output
        elif output == "ERROR":
            raise "ERROR occured in AP transaction"

    printdbg("DATA or STATUS")
    output = ''
    data=''
    t1=float(time.time())
    while True:
        data = ser.read(1)
        #printdbg(data)
        output += data
        t2=float(time.time())
        if timeout==0:
            if data == '\n' :
                printdbg("CR")
                break
        else:
            if data == '\n' or t2-t1>timeout:
                break

    output = output.strip()

    printdbg("READ DATA or STATUS:" + output)
    if nochk:
        print ("<--" + output)
        return output

    if output == "OK":
        print ("<--" + output)
        printdbg("READ STATUS: OK")
        return output
    elif output == "ERROR":
        print ("<--" + output)
        printdbg("READ STATUS: ERROR")
        if nochk:
            return output
        else:
            raise "ERROR occured in AT transaction"
    else:

        retval = output
        printdbg("READ DATA: " + retval)
        printdbg("STATUS")
        print ("<--" + retval)
        #more data coming
        output = ''
        data=''
        t1=float(time.time())
        while True:
            data = ser.read(1)
            #printdbg(data)
            output += data
            t2=float(time.time())
            if timeout==0:
                if data == '\n' :
                    printdbg("CR")
                    break
            else:
                if data == '\n' or t2-t1>timeout:
                    break

        output = output.strip()
        printdbg("READ STATUS 2:" + output)
        if output == "OK":
            #printdbg("READ STATUS2: OK")
            printdbg("RETVAL: " + retval)
            print ("<--" + output)
            return retval
        elif output == "ERROR":
            print ("<--" + output)
            #printdbg("READ STATUS: ERROR")
            if nochk:
                return output
            else:
                raise "ERROR occured in AT transaction"

    raise "AT_Read Shouldn't reach here..."


def AT_Read_Scan_Data(ser, timeout=0, nochk = False):

    scan_list = []
    #print("AT_Read")
    echo = 1
    if echo == 1:
        printdbg("ECHO")
        output = ''
        data=''
        t1=float(time.time())
        while True:
            data = ser.read(1)
            #printdbg(data)
            output += data
            t2=float(time.time())
            if timeout==0:
                if data == '\n' :
                    printdbg("CR")
                    break
            else:
                if data == '\n' or t2-t1>timeout:
                    break
        printdbg("READ ECHO:" + output)
        if output == "OK":
            return output
        elif output == "ERROR":
            raise "ERROR occured in AP transaction"

    printdbg("SCAN DATA")
    output = ''
    data=''
    t1=float(time.time())
    while True:
        data = ser.read(1)
        #printdbg(data)
        output += data
        t2=float(time.time())
        if timeout==0:
            if data == '\n' :
                printdbg("CR")
                output = output.strip()
                printdbg(output)
                if output == "Scanning...":
                    output = ''
                    continue
                if output == "Scan Completed...":
                    break
                scan_list.append(output)
                output = ''
                continue

        else:
            if data == '\n' or t2-t1>timeout:
                CodelessError("TODO, Not implemented")

    #print scan_list
    printdbg("STATUS")
    output = ''
    t1 = float(time.time())
    while True:
        data = ser.read(1)
        printdbg(data)
        output += data
        t2 = float(time.time())
        if timeout == 0:
            if data == '\n':
                printdbg("CR")
                break
        else:
            if data == '\n' or t2 - t1 > timeout:
                break

    output = output.strip()
    printdbg("READ DATA or STATUS:" + output)
    if output == "OK":
        print ("<--" + output)
        printdbg("READ STATUS: OK")
        return scan_list
    elif output == "ERROR":
        print ("<--" + output)
        printdbg("READ STATUS: ERROR")
        if nochk:
            return output
        else:
            raise "ERROR occured in AT transaction"

    raise "AT_Read Shouldn't reach here..."



def read_thread(serialPort):

    str = ""
    while True:
        read = serialPort.read(1)
        #print(read)
        str += read
        if read == "\n":
            print ("<--" + str)
            str = ""


def main():
    #signal.signal(signal.SIGINT, signal_handler)

    total = len(sys.argv)
    if total < 3:
        raise Exception("missing input arguement.\n Usage: python codeless.py COM116 80:EA:CA:80:66:01")
    cmdargs = str(sys.argv)
    comPort = sys.argv[1]
    bdaddr = sys.argv[2]
    print "Using port %s" % comPort
    print "Serial version %s" % serial.VERSION

    ser = serial.Serial()
    #ser.port = "/dev/ttyUSB0"
    ser.port = comPort
    #ser.port = "/dev/ttyS2"
    ser.baudrate = 57600
    ser.bytesize = serial.EIGHTBITS #number of bits per bytes
    ser.parity = serial.PARITY_NONE #set parity check: no parity
    ser.stopbits = serial.STOPBITS_ONE #number of stop bits
    #ser.timeout = None          #block read
    ser.timeout = None            #non-block read
    #ser.timeout = 2              #timeout block read
    ser.xonxoff = False     #disable software flow control
    ser.rtscts = False     #disable hardware (RTS/CTS) flow control
    ser.dsrdtr = False       #disable hardware (DSR/DTR) flow control
    ser.writeTimeout = 0     #timeout for write

    try:
        ser.open()
    except Exception, e:
        print "error open serial port: " + str(e)
        exit()

    if not ser.isOpen():
        print "cannot open serial port "
        exit()

    ser.flushInput() #flush input buffer, discarding all its contents
    ser.flushOutput()#flush output buffer, aborting current output
                     #and discard all that is in buffer


    #t = threading.Thread(target=read_thread, kwargs={'serialPort': ser})
    #t.daemon = True
    #t.start()

    #print "Testing connectivity"
    #AT_Write(ser, "ATI")
    #AT_Write(ser, "ATE=1")

    #AT_Write(ser, "ATI")

    #AT+RANDOM
    #test_001(ser)

    #AT+CURSOR
    #test_002(ser)

    #AT+BDADDR
    #test_003(ser)

    #AT+BATT
    #test_004(ser)

    #AT
    #test_005(ser)

    # Peripheral Advertising
    #test_006(ser)

    # Central scan
    #test_007(ser, bdaddr)

    # Central connect
    #test_008(ser, bdaddr)

    # AT Peripheral + sleep
    #test_009(ser)

    # AT+PRINT
    # test_010(ser)

    # AT + IOCFG
    #test_011(ser)

    # AT + SLEEEP
    #test_012(ser)

    # AT + ADC
    #test_013(ser)


    # AT + I2C
    #test_014(ser)

    # AT + I2C on remote board
    test_015(ser, bdaddr)

    #AT+NOTVALIDCOMMAND
    #test_NEG_001(ser)

    #AT+TOOBIGCOMMAND
    #test_NEG_002(ser)

    #AT+ATZPARTOFCOMMAND
    #test_NEG_003(ser)

    #t.join(10)

    print("Test complete")


#Connect Disconnect Test Codeless as central
def test_001_OLD(ser, bdaddr):


    AT_Write(ser, "AT+ADVSTOP")
    AT_Write(ser, "AT+CENTRAL")


    for loop in range(TEST_001_LOOPS):
        print("Loop %d" % loop)
        AT_Write(ser, "AT+GAPSCAN")
        time.sleep(10)
        AT_Write(ser, "AT+GAPCONNECT=%s,P" % bdaddr)
        time.sleep(5)
        print ("query status after connect")
        AT_Write(ser, "AT+GAPSTATUS")
        AT_Write(ser, "AT+GAPDISCONNECT")
        time.sleep(5)
        print ("query status after disconnect")
        AT_Write(ser, "AT+GAPSTATUS")


#ADV start stop
def test_002_OLD(ser):


    AT_Write(ser, "AT+GAPDISCONNECT")
    AT_Write(ser, "AT+ADVSTOP")
    AT_Write(ser, "AT+PERIPHERAL")


    for loop in range(TEST_002_LOOPS):
        advinterval = random.randint(100, 3000)
        print("Loop %d, adv interval : %d" % (loop, advinterval))
        AT_Write(ser, "AT+ADVSTART=%d" % advinterval)
        time.sleep(1)
        AT_Write(ser, "AT+ADVSTOP")
        #time.sleep(1)
        AT_Write(ser, "AT+GAPSTATUS")


#
def test_003_OLD(ser):
    AT_Write(ser, "ATE=1")
    for loop in range(10):
        print("Loop %d" % loop)
        AT_Write(ser, "AT+GAPSTATUS")
        AT_Write(ser, "AT+GAPDISCONNECT")
        time.sleep(2)
        AT_Write(ser, "AT+GAPSTATUS")


#AT+RANDOM
def test_001(ser):

    print_test("Testing AT+RANDOM")

    AT_Write(ser, "ATZ")
    r = AT_Read(ser)

    AT_Write(ser, "ATI")
    r = AT_Read(ser)

    AT_Write(ser, "ATE=1")
    r = AT_Read(ser)

    for loop in range(100):
        print("Loop %d" % loop)
        AT_Write(ser, "AT+RANDOM")
        r = AT_Read(ser)
        #print r
        #time.sleep(2)

#AT+CURSOR
def test_002(ser):
    print_test("Testing AT+CURSOR")

    AT_Write(ser, "ATZ")
    r = AT_Read(ser)

    AT_Write(ser, "ATI")
    r = AT_Read(ser)

    AT_Write(ser, "ATE=1")
    r = AT_Read(ser)

    raw_input('Open Power profiler and observe cursor markings. Press any key to continue.')
    for loop in range(100):
        print("Loop %d" % loop)
        AT_Write(ser, "AT+CURSOR")
        r = AT_Read(ser)
        #print r
        #time.sleep(2)

#AT+BDADDR
def test_003(ser):
    print_test("Testing AT+BDADDR")

    AT_Write(ser, "ATZ")
    r = AT_Read(ser)

    AT_Write(ser, "ATI")
    r = AT_Read(ser)

    AT_Write(ser, "ATE=1")
    r = AT_Read(ser)

    AT_Write(ser, "AT+BDADDR")
    r = AT_Read(ser)
    answer = raw_input('Is the returned BD Address the one defined in OTP or NVMS? Y/N')
    #print answer
    if(answer.upper() == "N" or answer.upper() == "NO"):
        raise("Test failed")

#AT+BATT
def test_004(ser):
    print_test("Testing AT+BATT")

    AT_Write(ser, "ATZ")
    r = AT_Read(ser)

    AT_Write(ser, "ATI")
    r = AT_Read(ser)

    AT_Write(ser, "ATE=1")
    r = AT_Read(ser)

    AT_Write(ser, "AT+BATT")
    r = AT_Read(ser)
    answer = raw_input('Is the returned Battery level OK? Y/N')
    #print answer
    if(answer.upper() == "N" or answer.upper() == "NO"):
        raise("Test failed")

#AT
def test_005(ser):
    print_test("Testing AT command")

    AT_Write(ser, "ATZ")
    r = AT_Read(ser)

    AT_Write(ser, "ATI")
    r = AT_Read(ser)

    AT_Write(ser, "ATE=1")
    r = AT_Read(ser)

    AT_Write(ser, "AT")
    r = AT_Read(ser)

# AT Advertising interval
def test_006(ser):

    print_test("Testing AT Advertising interval")

    AT_Write(ser, "ATZ")
    r = AT_Read(ser)
    AT_Write(ser, "ATE=1")
    r = AT_Read(ser)
    AT_Write(ser, "AT+GAPDISCONNECT")
    r = AT_Read(ser, nochk=True)
    AT_Write(ser, "AT+ADVSTOP")
    r = AT_Read(ser, nochk=True)
    AT_Write(ser, "AT+PERIPHERAL")
    r = AT_Read(ser, nochk=True)
    AT_Write(ser, "AT+GAPSTATUS")
    r = AT_Read(ser)
    gapstatus = r.split(',')
    if int(gapstatus[0]) != GAPSTATUS_PERIPHERAL:
        CodelessError("DUT did not report status as peripheral")
    if int(gapstatus[1]) != GAPSTATUS_DISCONNECTED:
        CodelessError("DUT did not report status as disconnected")
    AT_Write(ser, "AT+PERIPHERAL")
    r = AT_Read(ser, nochk=True)

    AT_Write(ser, "AT+ADVDATA=11:07:86:6D:3B:04:E6:74:40:DC:9C:05:B7:F9:1B:EC:6E:83:09:09:43:6F:64:65:4C:65:73:73")
    r = AT_Read(ser)
    AT_Write(ser, "AT+ADVRESP=11:07:86:6D:3B:04:E6:74:40:DC:9C:05:B7:F9:1B:EC:6E:83:09:09:43:6F:64:65:4C:65:73:65")
    r = AT_Read(ser)

    for advinterval in [100, 300, 500]:
        # advinterval = random.randint(100, 3000)
        print("Loop: adv interval : %d" % advinterval)
        AT_Write(ser, "AT+ADVSTART=%d" % advinterval)
        r = AT_Read(ser)
        time.sleep(5)
        AT_Write(ser, "AT+ADVSTOP")
        r = AT_Read(ser)
        # time.sleep(1)
        AT_Write(ser, "AT+GAPSTATUS")
        r = AT_Read(ser)

# AT Central scan
def test_007(ser, bdaddr2find):

    print_test("Testing AT Central scan")

    AT_Write(ser, "ATZ")
    r = AT_Read(ser)
    AT_Write(ser, "ATE=1")
    r = AT_Read(ser)
    AT_Write(ser, "AT+GAPDISCONNECT")
    r = AT_Read(ser, nochk=True)
    AT_Write(ser, "AT+ADVSTOP")
    r = AT_Read(ser, nochk=True)
    AT_Write(ser, "AT+CENTRAL")
    r = AT_Read(ser, nochk=True)
    AT_Write(ser, "AT+GAPSTATUS")
    r = AT_Read(ser)
    gapstatus = r.split(',')
    if int(gapstatus[0]) != GAPSTATUS_CENTRAL:
        CodelessError("DUT did not report status as central")
    if int(gapstatus[1]) != GAPSTATUS_DISCONNECTED:
        CodelessError("DUT did not report status as disconnected")

    AT_Write(ser, "AT+GAPSCAN")
    r = AT_Read_Scan_Data(ser)
    print r
    found = False
    for ble in r:
        bdaddr = ble.split(',')[0]
        bdaddr = bdaddr[3:].strip()
        if bdaddr == bdaddr2find:
            found = True

    if found == False:
        CodelessError("Did not find desired BLE advertiser %s" % bdaddr2find)

#AT Central connect
def test_008(ser, bdaddr2connect):

    print_test("Testing AT Central connect ,public address")

    AT_Write(ser, "ATZ")
    r = AT_Read(ser)
    AT_Write(ser, "ATE=1")
    r = AT_Read(ser)
    AT_Write(ser, "AT+GAPDISCONNECT")
    r = AT_Read(ser, nochk=True)
    AT_Write(ser, "AT+ADVSTOP")
    r = AT_Read(ser, nochk=True)
    AT_Write(ser, "AT+CENTRAL")
    r = AT_Read(ser, nochk=True)
    AT_Write(ser, "AT+GAPSTATUS")
    r = AT_Read(ser)
    gapstatus = r.split(',')
    if int(gapstatus[0]) != GAPSTATUS_CENTRAL:
        CodelessError("DUT did not report status as central")
    if int(gapstatus[1]) != GAPSTATUS_DISCONNECTED:
        CodelessError("DUT did not report status as disconnected")

    AT_Write(ser, "AT+GAPSCAN")
    r = AT_Read_Scan_Data(ser)
    print r
    found = False
    for ble in r:
        bdaddr = ble.split(',')[0]
        bdaddr = bdaddr[3:].strip()
        if bdaddr == bdaddr2connect:
            found = True

    if found == False:
        CodelessError("Did not find desired BLE advertiser %s" % bdaddr2find)

    AT_Write(ser, "AT+GAPCONNECT="+bdaddr2connect+",P")
    r = AT_Read(ser, nochk=True)
    if (r != "Connecting..."):
        CodelessError("Did not get Connecting message")
    r = AT_Read(ser)

    time.sleep(5)

    AT_Write(ser, "AT+GAPSTATUS")
    r = AT_Read(ser)
    gapstatus = r.split(',')
    if int(gapstatus[0]) != GAPSTATUS_CENTRAL:
        CodelessError("DUT did not report status as central")
    if int(gapstatus[1]) != GAPSTATUS_CONNECTED:
        CodelessError("DUT did not report status as connected")


    AT_Write(ser, "AT+GAPDISCONNECT")
    r = AT_Read(ser, nochk=True)
    if(r != "Disconnecting..."):
        CodelessError("Did not get Disconnecting message")
    r = AT_Read(ser)
    AT_Write(ser, "AT+GAPSTATUS")
    r = AT_Read(ser)
    gapstatus = r.split(',')
    if int(gapstatus[0]) != GAPSTATUS_CENTRAL:
        CodelessError("DUT did not report status as central")
    if int(gapstatus[1]) != GAPSTATUS_DISCONNECTED:
        CodelessError("DUT did not report status as disconnected")

# AT Peripheral
def test_009(ser):

    print_test("Testing AT Peripheral")

    AT_Write(ser, "ATZ")
    r = AT_Read(ser)
    AT_Write(ser, "ATE=1")
    r = AT_Read(ser)
    AT_Write(ser, "AT+GAPDISCONNECT")
    r = AT_Read(ser, nochk=True)
    if r == "Disconnecting...":
        # wait for "OK or ERROR"
        r = AT_Read(ser)
    AT_Write(ser, "AT+ADVSTOP")
    r = AT_Read(ser, nochk=True)
    AT_Write(ser, "AT+PERIPHERAL")
    r = AT_Read(ser, nochk=True)
    AT_Write(ser, "AT+GAPSTATUS")
    r = AT_Read(ser)
    gapstatus = r.split(',')
    if int(gapstatus[0]) != GAPSTATUS_PERIPHERAL:
        CodelessError("DUT did not report status as peripheral")
    if int(gapstatus[1]) != GAPSTATUS_DISCONNECTED:
        CodelessError("DUT did not report status as disconnected")
    AT_Write(ser, "AT+PERIPHERAL")
    r = AT_Read(ser, nochk=True)

    AT_Write(ser,
             "AT+ADVDATA=11:07:86:6D:3B:04:E6:74:40:DC:9C:05:B7:F9:1B:EC:6E:83:09:09:43:6F:64:65:4C:65:73:73")
    r = AT_Read(ser)
    AT_Write(ser,
             "AT+ADVRESP=11:07:86:6D:3B:04:E6:74:40:DC:9C:05:B7:F9:1B:EC:6E:83:09:09:43:6F:64:65:4C:65:73:65")
    r = AT_Read(ser)

    advinterval = 100
    AT_Write(ser, "AT+ADVSTART=%d" % advinterval)
    r = AT_Read(ser)

    raw_input('Connect to DUT using 3rd party central. Press any key when done')

    AT_Write(ser, "AT+GAPSTATUS")
    r = AT_Read(ser)
    gapstatus = r.split(',')
    if int(gapstatus[0]) != GAPSTATUS_PERIPHERAL:
        CodelessError("DUT did not report status as peripheral")
    if int(gapstatus[1]) != GAPSTATUS_CONNECTED:
        CodelessError("DUT did not report status as connected")

    # Port 1 pin 3 output
    AT_Write(ser, "AT+IOCFG=13,4")

    for loop in range(1000):
        AT_Write(ser, "AT+SLEEP=0")
        r = AT_Read(ser)
        AT_Write(ser, "AT+IO=13,%d" % (loop % 2))
        r = AT_Read(ser)
        AT_Write(ser, "AT+SLEEP=1")
        r = AT_Read(ser)
        time.sleep(1)
        AT_Write(ser, "AT+PRINT=" + dummy_text[1:random.randint(1, 100)])
        r = AT_Read(ser, nochk=True)
        r = AT_Read(ser)

# AT+PRINT
def test_010(ser):

    print_test("Testing AT+PRINT")
    for loop in range(1000):
        AT_Write(ser, "AT+PRINT="+dummy_text[1:random.randint(1,100)])
        r = AT_Read(ser, nochk=True)
        r = AT_Read(ser)

# AT+IOCFG
def test_011(ser):

    print_test("Testing AT+IOCFG")
    # Port 1 pin 3 output
    AT_Write(ser, "AT+IOCFG=13,4")
    r = AT_Read(ser)
    for loop in range(1000):
        AT_Write(ser, "AT+IO=13,%d" % (loop%2))
        r = AT_Read(ser)

# AT+SLEEP
def test_012(ser):

    print_test("Testing AT+IOCFG")
    # Port 1 pin 3 output
    AT_Write(ser, "AT+IOCFG=13,4")
    r = AT_Read(ser)
    for loop in range(10):
        AT_Write(ser, "AT+SLEEP=0")
        r = AT_Read(ser)
        AT_Write(ser, "AT+IO=13,%d" % (loop % 2))
        r = AT_Read(ser)
        AT_Write(ser, "AT+SLEEP=1")
        r = AT_Read(ser)
        time.sleep(1)

# I2C
def test_014(ser):

    print_test("Testing I2C")
    # Port 1 pin 1 IO_FUNC_I2C_CLOCK
    AT_Write(ser, "AT+IOCFG=11,7")
    r = AT_Read(ser)

    # Port 0 pin 2 IO_FUNC_I2C_DATA
    AT_Write(ser, "AT+IOCFG=2,8")
    r = AT_Read(ser)

    AT_Write(ser, "AT+I2CSCAN")
    r = AT_Read(ser)

    AT_Write(ser, "AT+I2CCFG=7,400,16")
    r = AT_Read(ser)

    #BME680 chip id register
    AT_Write(ser, "AT+I2CREAD=0x76,0xD0")
    r = AT_Read(ser)
    if r != "0x6101":
        CodelessError("Did not read chip id correctly")

    # BME680 mode register
    AT_Write(ser, "AT+I2CWRITE=0x76,0x74,0x01")
    r = AT_Read(ser)

    time.sleep(1)


    # BME680 mode register
    AT_Write(ser, "AT+I2CREAD=0x76,0x74")
    r = AT_Read(ser)


# I2C on remote board
def test_015(ser, bdaddr2connect):

    print_test("Testing I2C remote")
    raw_input('Start advertising on remote device. Press any key to continue.')

    AT_Write(ser, "ATZ")
    r = AT_Read(ser)
    AT_Write(ser, "ATE=1")
    r = AT_Read(ser)
    AT_Write(ser, "AT+GAPDISCONNECT")
    r = AT_Read(ser, nochk=True)
    AT_Write(ser, "AT+ADVSTOP")
    r = AT_Read(ser, nochk=True)
    AT_Write(ser, "AT+CENTRAL")
    r = AT_Read(ser, nochk=True)
    AT_Write(ser, "AT+GAPSTATUS")
    r = AT_Read(ser)
    gapstatus = r.split(',')
    if int(gapstatus[0]) != GAPSTATUS_CENTRAL:
        CodelessError("DUT did not report status as central")
    if int(gapstatus[1]) != GAPSTATUS_DISCONNECTED:
        CodelessError("DUT did not report status as disconnected")


    #bd = "C7:69:72:05:12:54"
    AT_Write(ser, "AT+GAPCONNECT=" + bdaddr2connect + ",R")
    r = AT_Read(ser, nochk=True)
    if (r != "Connecting..."):
        CodelessError("Did not get Connecting message")
    r = AT_Read(ser)

    time.sleep(5)

    AT_Write(ser, "AT+GAPSTATUS")
    r = AT_Read(ser)
    gapstatus = r.split(',')
    if int(gapstatus[0]) != GAPSTATUS_CENTRAL:
        CodelessError("DUT did not report status as central")
    if int(gapstatus[1]) != GAPSTATUS_CONNECTED:
        CodelessError("DUT did not report status as connected")




    # Port 1 pin 1 IO_FUNC_I2C_CLOCK
    AT_Write(ser, "ATr+IOCFG=11,7")
    r = AT_Read(ser)

    # Port 0 pin 2 IO_FUNC_I2C_DATA
    AT_Write(ser, "ATr+IOCFG=2,8")
    r = AT_Read(ser)

    AT_Write(ser, "ATr+I2CSCAN")
    r = AT_Read(ser)

    AT_Write(ser, "ATr+I2CCFG=7,400,16")
    r = AT_Read(ser)

    #BME680 chip id register
    AT_Write(ser, "ATr+I2CREAD=0x76,0xD0")
    r = AT_Read(ser)
    if r != "0x6101":
        CodelessError("Did not read chip id correctly")

    # BME680 mode register
    AT_Write(ser, "ATr+I2CWRITE=0x76,0x74,0x01")
    r = AT_Read(ser)

    time.sleep(1)


    # BME680 mode register
    AT_Write(ser, "ATr+I2CREAD=0x76,0x74")
    r = AT_Read(ser)


    AT_Write(ser, "AT+GAPDISCONNECT")
    r = AT_Read(ser, nochk=True)
    if (r != "Disconnecting..."):
        CodelessError("Did not get Disconnecting message")
    r = AT_Read(ser)
    AT_Write(ser, "AT+GAPSTATUS")
    r = AT_Read(ser)
    gapstatus = r.split(',')
    if int(gapstatus[0]) != GAPSTATUS_CENTRAL:
        CodelessError("DUT did not report status as central")
    if int(gapstatus[1]) != GAPSTATUS_DISCONNECTED:
        CodelessError("DUT did not report status as disconnected")



#AT+NOTVALIDCOMMAND
def test_NEG_001(ser):

    AT_Write(ser, "ATZ")
    r = AT_Read(ser)

    AT_Write(ser, "ATI")
    r = AT_Read(ser)

    AT_Write(ser, "ATE=1")
    r = AT_Read(ser)

    AT_Write(ser, "AT+NOTVALIDCOMMAND")
    r = AT_Read(ser, nochk=True)
    if r != "ERROR":
        raise "Should return Error for not valid command"

    AT_Write(ser, "AT")
    r = AT_Read(ser)

#AT+TOOBIGCOMMAND
def test_NEG_002(ser):

    AT_Write(ser, "ATZ")
    r = AT_Read(ser)

    AT_Write(ser, "ATI")
    r = AT_Read(ser)

    AT_Write(ser, "ATE=1")
    r = AT_Read(ser)

    AT_Write(ser, "AT+TOOBIGCOMMANDTOOBIGCOMMANDTOOBIGCOMMANDTOOBIGCOMMANDTOOBIGCOMMANDTOOBIGCOMMANDTOOBIGCOMMANDTOOBIGCOMMANDTOOBIG")
    #AT_Write(ser, "ATI")
    r = AT_Read(ser, nochk=True)
    if r != "ERROR":
        raise "Should return Error for not valid too big command"

    AT_Write(ser, "AT")
    r = AT_Read(ser)

#AT+ATZPARTOFCOMMAND
def test_NEG_003(ser):

    AT_Write(ser, "ATZ")
    r = AT_Read(ser)

    AT_Write(ser, "ATI")
    r = AT_Read(ser)

    AT_Write(ser, "ATE=1")
    r = AT_Read(ser)

    AT_Write(ser, "AT+ATZPARTOFCOMMAND")
    r = AT_Read(ser, nochk=True)
    if r != "ERROR":
        raise "Should return Error for not valid part of command"

    AT_Write(ser, "AT")
    r = AT_Read(ser)




if __name__ == '__main__':

    try:
        main()
    except KeyboardInterrupt:
        print "C-C"
        exit(3)
