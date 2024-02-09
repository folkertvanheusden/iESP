#! /usr/bin/python3

import os
import random
import sys
import time

dev=sys.argv[1]
bs=1
dt=5

fd = os.open(dev, os.O_RDONLY)
dev_size = os.lseek(fd, 0, os.SEEK_END)

for m in range(20):
    bs *= 2

    n = 0

    start = time.time()
    while True:
        now = time.time()
        if now >= start + dt:
            break
        offset = random.randint(0, dev_size) & ~(bs - 1)
        os.lseek(fd, offset, os.SEEK_SET)
        os.read(fd, bs)
        n += 1

    print(m, bs, n * dt / (now - start))
