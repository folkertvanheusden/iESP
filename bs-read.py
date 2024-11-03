#! /usr/bin/python3

import os
import sys
import threading
import time

dev = sys.argv[1]
bs_min = int(sys.argv[2])
bs_steps = int(sys.argv[3])
duration = float(sys.argv[4])
linear = sys.argv[5] == 'linear'

offset = 0

def do_(dev, bs, duration):
    global offset
    fd = os.open(dev, os.O_RDONLY)
    dev_size = os.lseek(fd, 0, os.SEEK_END)
    os.posix_fadvise(fd, 0, dev_size, os.POSIX_FADV_DONTNEED)
    end = time.time() + duration

    n = 0
    while time.time() < end:
        if offset + bs > dev_size:
            offset = 0

        os.lseek(fd, offset, os.SEEK_SET)
        os.posix_fadvise(fd, offset, bs, os.POSIX_FADV_DONTNEED)
        os.read(fd, bs)

        offset += bs
        n += 1

    os.close(fd)

    return n

fhb = open('plot-bw-iop.dat', 'w')
for bs_it in range(bs_steps):
    if linear:
        bs = bs_min * (bs_it + 1)
    else:
        bs = bs_min << bs_it
    iop = do_(dev, bs, duration)
    out = f'{bs} {iop} {iop * bs} {iop / duration} {iop * bs / duration}'
    print(out)
    fhb.write(out + '\n')
fhb.close()
