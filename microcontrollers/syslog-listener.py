#! /usr/bin/python3

import time
import socket

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(('', 514))
while True:
    data, who = s.recvfrom(4096)
    print(time.ctime(), who, data)
