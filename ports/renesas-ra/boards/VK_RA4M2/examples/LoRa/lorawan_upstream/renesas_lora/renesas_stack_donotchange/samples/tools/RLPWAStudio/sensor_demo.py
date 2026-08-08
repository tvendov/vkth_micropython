#
# Copyright (c) 2021 Renesas Electronics Corporation
# This software is released under the MIT License.
# http://opensource.org/licenses/mit-license.php
#
# Sensor Demo : LPWA Stduio -> InfluxDB/Grafana
#

import datetime, json, base64
from socket import *
from influxdb import InfluxDBClient

db = InfluxDBClient(host='localhost', port='8086', database='home')
udp = socket(AF_INET, SOCK_DGRAM)
udp.bind(('127.0.0.1', 54321))

while True:
    msg, addr = udp.recvfrom(1024)
    print('[LOG] IN: ', msg)
    try:
        data = json.loads(msg.decode())['rxpk'][0]['data']
        x = base64.b64decode(data)
        eui = bytes([x[0]]).hex() # lowest byte of the DevEUI
        hum = int.from_bytes(x[1:3],'big',signed=False) / 10
        temp = int.from_bytes(x[3:5],'big',signed=True) / 10
        data = [{
            'fields': {'temperature': temp, 'humidity': hum},
            'measurement': 'sensor_demo',
            'time': datetime.datetime.utcnow(),
            'tags':{'deveui': eui}}]
        db.write_points(data)
        print(f'[LOG] OUT: eui={eui}, hum={hum}, temp={temp}')
    except Exception as e:
        print('[LOG] ERROR: ', e)