#! /usr/bin/python3

import hashlib
import os
import random
import sys
import time

dev = sys.argv[1]
blocksize = int(sys.argv[2])

seed = int(time.time())
fd = os.open(dev, os.O_RDWR)
dev_size = os.lseek(fd, 0, os.SEEK_END)

n_blocks = dev_size // blocksize
seen = [ None ] * n_blocks

def gen_block(size, offset, seed2):
    m = hashlib.md5(offset.to_bytes(8, 'big') + seed.to_bytes(8, 'big') + seed2.to_bytes(2, 'big')).digest()
    out = bytearray()
    while len(out) < size:
        out += bytearray(m)
        m = hashlib.md5(m).digest()
    return out

n = 0
verified = 0
start = time.time()
prev = start
w = 0

while True:
    offset = random.randint(0, dev_size) & ~(blocksize - 1)
    nr = offset // blocksize

    if seen[nr] != None:
        # verify
        b = gen_block(blocksize, offset, seen[nr])

        data = os.pread(fd, blocksize, offset)
        assert(data == b)
        verified += 1

    else:
        w += 1

    seen[nr] = random.randint(0, 65535)
    b = gen_block(blocksize, offset, seen[nr])
    os.pwrite(fd, b, offset)

    n += 1

    now = time.time()
    if now - prev >= 1:
        print(f'total: {n}, n/s: {int(n / (now - start))}, percent done: {w * 100 / n_blocks:.2f}, n verified: {verified}')
        prev = now
