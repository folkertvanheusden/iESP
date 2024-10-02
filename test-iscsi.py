#! /usr/bin/python3

import os
import socket

addr = ('127.0.0.1', 3260)

while True:
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect(addr)
            while True:
                data = bytearray(os.urandom(48))
                s.send(data)
    except Exception as e:
        print(e)
