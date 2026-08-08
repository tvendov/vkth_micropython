"""
* This module provides utility functions for communicating with a codeless device
* over uart and/or ble with python. This is can be used for automated testing of
* codeless builds - but can also be used for interacting with one or multiple CodeLess
* devices from a sbc such as raspbery pi.
*
* Copyright (C) 2018 Dialog Semiconductor.
 * This computer program includes Confidential, Proprietary Information
 * of Dialog Semiconductor. All Rights Reserved.
 *
"""

import sys
try:
    import bluepy
except ImportError:
    import pygatt
import serial
import time
from enum import Enum


class CodelessLink(Enum):
    BLE = 0
    UART = 1


class CodelessStatus(Enum):
    CODELESS_OK = 0
    CODELESS_ERROR = 1


class codeless:
    def __init__(self, link_type, link_address):
        """
            Initializes a codeless instance.
            Linktype: Can be of type CodelessLink.
            LinkAddress: In case of BLE, this is the codeless peers bluetooth address
                         In case of UART, this is the codeless peers device instance
            Return status
            Example usage:
                remote_peer = codeless(CodelessLink.BLE, 'D5:59:63:DA:9A:13')
                local_peer = codeless(CodelessLink.UART, '\dev\ttyACM0')
        """
        self.link_type = link_type
        self.link_address = link_address
        self.reply_string = ""
        self.device_connected = 0
        self.status = CodelessStatus.CODELESS_ERROR
        if self.link_type == CodelessLink.BLE:
            self.callback_notification_flag = 0
            try:
                # Handle host operating system
                if sys.platform == "linux" or sys.platform == "linux2":
                    self.adapter = bluepy.btle.Peripheral()
                elif sys.platform == "win32":
                    self.adapter = pygatt.BGAPIBackend()
                else:
                    print("Only Linux and Windows with BlueGiga devices supported.")
            except:
                print("Could not start pygatt adapter")
        elif self.link_type == CodelessLink.UART:
            ser = serial.Serial()
            self.device = ser
            self.reply_received = 0

    def stop(self):
        """
           Disconnect from codeless slave
        """
        if self.link_type == CodelessLink.BLE:
            # Handle host operating system
            if sys.platform == "linux" or sys.platform == "linux2":
                self.adapter.disconnect()
            elif sys.platform == "win32":
                self.adapter.stop()
            else:
                print("Only Linux and Windows with BlueGiga devices supported.")
        elif self.link_type == CodelessLink.UART:
            self.device.close()
        self.device_connected = 0

#    def handle_ble_slave_notification(self, handle, value):
#        """
#           Callback trigger every time a connected slave codeless
#           produces a notification that its outbound characteristic is written
#           i.e returning a reply after a command.
#        """
#        self.callback_notification_flag = 1

    def connect(self):
        """
           Connect to codeless instance using parameters during instance initialization.
           Return status
        """
        if self.link_type == CodelessLink.BLE:
            # Connect to peer device.
            try:
                # Handle host operating system
                if sys.platform == "linux" or sys.platform == "linux2":
                    self.adapter.connect(self.link_address, bluepy.btle.ADDR_TYPE_PUBLIC)
                elif sys.platform == "win32":
                    self.adapter.start()
                    self.device = self.adapter.connect(self.link_address, 10)
                else:
                    print("Only Linux and Windows with BlueGiga devices supported.")
                # Update instance status as connected.
                self.device_connected = 1
                self.status = CodelessStatus.CODELESS_OK
            except:
                print("Could not start BLE device")

        elif self.link_type == CodelessLink.UART:
            self.device.port = self.link_address
            self.device.baudrate = 57600
            self.device.bytesize = serial.EIGHTBITS  # number of bits per bytes
            self.device.parity = serial.PARITY_NONE  # set parity check: no parity
            self.device.stopbits = serial.STOPBITS_ONE  # number of stop bits
            self.device.timeout = None  # non-block read
            self.device.xonxoff = False  # disable software flow control
            self.device.rtscts = False  # disable hardware (RTS/CTS) flow control
            self.device.dsrdtr = False  # disable hardware (DSR/DTR) flow control
            self.device.writeTimeout = 0  # timeout for write

            try:
                self.device.open()
                self.device.reset_input_buffer()
                self.device.reset_output_buffer()
            except:
                print("error on opening serial port")
                exit()
            if not self.device.isOpen():
                print("cannot open serial port")
                exit()
            # Update instance status as connected.
            self.status = CodelessStatus.CODELESS_OK
            self.device_connected = 1
        else:
            self.status = CodelessStatus.CODELESS_ERROR
        return self.status

    def send_command(self, command_string, timeout_in_sec):
        """
            Send a string with AT command to connected codeless device.
            If no response is returned in the specified timeout_in_sec time
            the function return with an empty string and a CODELESS_ERROR status
            Example usage:
                status, reply_string = local_peer.send_command_with_timeout("ATr+IO=10,1", 2)
        """
        # List of strings with approved at command termination strings.
        termination_list = ('OK\r\n', 'ERROR\r\n')
        # Calculate time elapsed
        start_time = time.time()
        # Initialize flags and empty string reply.
        self.reply_received = 0
        self.reply_string = ""
        self.status = CodelessStatus.CODELESS_ERROR

        # Check that device is already successfully connected.
        if self.device_connected == 1:

            # Send string depending on link type
            if self.link_type == CodelessLink.BLE:

                command = command_string + '\x00'

                self.callback_notification_flag = 0
                # Write to codeless inbound characteristic.
                b = command.encode()

                # Handle host operating system
                if sys.platform == "linux" or sys.platform == "linux2":
                    characteristics = self.adapter.getCharacteristics()
                    characteristics[3].write(b, withResponse=True)
                elif sys.platform == "win32":
                    self.device.char_write('914f8fb9-e8cd-411d-b7d1-14594de45425', b, 1)
                else:
                    print("Only Linux and Windows with BlueGiga devices supported.")

                time.sleep(0.5)
                while self.callback_notification_flag == 0:
                    #windows workaround - in order to read bgapi more than 23 bytes a patch to bgapi\device.py should be applied.
                    if sys.platform == "linux" or sys.platform == "linux2":
                        self.reply_string = characteristics[4].read()
                    elif sys.platform == "win32":
                        self.reply_string = self.device.char_read_long('3bb535aa-50b2-4fbe-aa09-6b06dc59a404')
                    else:
                        print("Only Linux and Windows with BlueGiga devices supported.")

                    current_line = self.reply_string.decode()
                    current_line = current_line[0:len(current_line)-1] # Remove null termination character
                    if current_line.endswith(termination_list):
                        return CodelessStatus.CODELESS_OK, self.reply_string.decode()
                    time.sleep(0.1)
                    elapsed_time = time.time() - start_time
                    if elapsed_time > timeout_in_sec:
                        print("timeout occurred!")
                        return CodelessStatus.CODELESS_ERROR, self.reply_string.decode()

            elif self.link_type == CodelessLink.UART:

                command = command_string + '\r'
                self.device.write(command.encode())

                # Add a small delay between uart write/read operations
                time.sleep(0.1)

                # start reading lines till a valid command termination string is reached or till timeout expires.
                while self.reply_received == 0:
                    while self.device.inWaiting() > 0:
                        bytes_to_read = self.device.inWaiting()
                        current_line = self.device.read(bytes_to_read)
                        self.reply_string = self.reply_string + current_line.decode()
                        elapsed_time = time.time() - start_time
                        if elapsed_time > timeout_in_sec:
                            print("Timeout occurred!")
                            return CodelessStatus.CODELESS_ERROR, self.reply_string
                    if self.reply_string.endswith(termination_list):
                        self.reply_received = 1
                    else:
                        time.sleep(0.1)  # Add some delay between consecutive reads.
                        elapsed_time = time.time() - start_time
                        if elapsed_time > timeout_in_sec:
                            print("Timeout occurred!")
                            return CodelessStatus.CODELESS_ERROR, self.reply_string

                self.status = CodelessStatus.CODELESS_OK

        return [self.status, self.reply_string]

    def read_output(self):
        """
            Read output from a codeless device if there is any. Generated output is not always a
            command reply, such is the case for the initialization text, or content generated
            by pipe command from a connected peer or ATr+PRINT. Other cases are aync error reports.
            Example usage:
                status, output_string = local_peer.read_output()
        """
        # Calculate time elapsed
        start_time = time.time()
        # Initialize flags and empty string reply.
        output_string = ""
        self.status = CodelessStatus.CODELESS_ERROR
        # Check that device is already successfully connected.
        if self.device_connected == 1:
            # Send string depending on link type
            if self.link_type == CodelessLink.BLE:
                if sys.platform == "linux" or sys.platform == "linux2":
                    characteristics = self.adapter.getCharacteristics()
                    output_string_bytes = characteristics[4].read()
                    output_string = output_string_bytes.decode()
                elif sys.platform == "win32":
                    output_string_bytes = self.device.char_read_long('3bb535aa-50b2-4fbe-aa09-6b06dc59a404')
                    output_string = output_string_bytes.decode()
                else:
                    print("Only Linux and Windows with BlueGiga devices supported.")
                self.status = CodelessStatus.CODELESS_OK

            elif self.link_type == CodelessLink.UART:

                if self.device.inWaiting() > 0:
                    bytes_to_read = self.device.inWaiting()
                    output_string_bytes = self.device.read(bytes_to_read)
                    output_string = output_string_bytes.decode()
                    self.status = CodelessStatus.CODELESS_OK
        return [self.status, output_string]


def load_bin_to_ram(serial_port, binary_dir):
    """
        Load binary image .bin from binary_dir directory to device connected at serial_port
        Example usage:
            status = load_bin_to_ram('COM22', 'codeless_585.bin')
    """
    ser = serial.Serial()
    ser.timeout = None
    ser.port = serial_port
    ser.baudrate = '57600'
    try:
        ser.open()
        ser.flushOutput()
        ser.flushInput()
        bin_image_file_desc = open(binary_dir, 'rb')
        a = bin_image_file_desc.read()
        bin_image_file_desc.seek(0)
        crc = 0
        for i in range(len(a)):
                byte = bin_image_file_desc.read(1)
                crc = crc ^ ord(byte)
        bin_image_file_desc.close()
        length = len(a)
        kb_length = length // 1024
        remainder_length = ((length % 1024) * 10) // 1024
        print("Waiting for UART boot request...", )
        stx_byte = 0
        ack_byte = 0
        crc_byte = 0
        # Send ATR command
        command = 'ATR\r'
        ser.write(command.encode())
        # Wait for STX
        while stx_byte != 2:
                byte = ser.read(1)
                if byte != "":
                    stx_byte = ord(byte)
        print("STX received!\r\n")
        # Send SOH
        ser.write(chr(1).encode())
        if length <= 65535:
                ser.write(bytes([length & 0xff]))  # send LSB of length
                ser.write(bytes([(length >> 8) & 0xff]))  # send MSB of length
        else:
            ser.write(chr(0).encode())  # send 0
            ser.write(chr(0).encode())  # send 0
            ser.write(bytes([(length - 65536) & 0xFF]))  # send LSB of length
            ser.write(bytes([((length - 65536) >> 8) & 0xFF]))  # send MSB of length
            ser.write(chr(1).encode())  # send length extension byte
        # Wait for ACK
        while ack_byte != 6:
                byte = ser.read(1)
                if byte != "":
                    ack_byte = ord(byte)
                    if ack_byte == 21:  # NACK received, exit
                        print("Error, NACK received.")
                        ser.close()
                        sys.exit()
        print("Start download...")
        print("Length: " + str(kb_length) + "." + str(remainder_length) + " kb (" + str(length) + " bytes)\r\n")
        ser.write(a)
        # Wait for CRC
        while crc_byte == 0:
                byte = ser.read(1)
                if byte != "":
                    crc_byte = ord(byte)
        # Check if CRC is OK
        if crc_byte != crc:
                print("CRC error! Download failed...")
                print("Calculated CRC: " + str(crc))
                print("Received CRC:   " + str(crc_byte))
                ser.close()
                return False
        else:
                print("Download completed successfully!")
                ser.write(chr(6).encode())
                ser.close()
                return True
    except:
        ser.close()
        print("Download failed")
        return False
