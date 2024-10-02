#! /usr/bin/python3

import os
import select
import socket

addr = ('127.0.0.1', 3260)

base = b'\x01\xc1\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00SD,\xfd\x00\x00\x02\x00X\x8c\xdf\xfb\x00\x00\x00\x04\x88\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x01\x00\x00'[0:48-16]

while True:
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect(addr)
            socket_list = [s]
            while True:
                cur_msg = base + bytearray(os.urandom(16))
                s.send(cur_msg)

                read_sockets, write_sockets, error_sockets = select.select(socket_list , [], [])
                for sock in read_sockets:
                    if sock == s:
                        sock.recv(65536)
    except Exception as e:
        print(e)
