#! /usr/bin/python3

import hashlib
import os
import random
import sys
import time

##### DO NOT RUN THIS ON A DEVICE WITH DATA! IT GETS ERASED! ######

if len(sys.argv) != 5:
    print(f'Usage: {sys.argv[0]} dev blocksize maxblockcount unique-percentage')
    print('dev:               block device')
    print('blocksize:         e.g. 512 or 4096')
    print('maxblockcount:     maximum number of blocks to write in one go')
    print('unique-percentage: for testing de-duplication devices')
    print()
    print(' ##### DO NOT RUN THIS ON A DEVICE WITH DATA! IT GETS ERASED! ###### ')
    print()
    sys.exit(1)

dev = sys.argv[1]  # device file
blocksize = int(sys.argv[2])  # size of each block (512, 4096, etc)
max_b = int(sys.argv[3])  # max. number of blocks in one go
unique_perc = int(sys.argv[4])  # how many of the blocks should be unique, %

random.seed()

seed = int(time.time())
fd = os.open(dev, os.O_RDWR | os.O_DIRECT)
dev_size = os.lseek(fd, 0, os.SEEK_END)

n_blocks = dev_size // blocksize
if dev_size % blocksize:
    print(f'Note: disk is not a multiple of {blocksize} in size ({dev_size})!')
seen = [ None ] * n_blocks

duplicate = 0xffffffff

def gen_block(size, offset, seed2):
    if seed2 == duplicate:
        m = hashlib.md5(seed.to_bytes(8, 'big')).digest()
    else:
        m = hashlib.md5(offset.to_bytes(8, 'big') + seed.to_bytes(8, 'big') + seed2.to_bytes(4, 'big')).digest()
    out = bytearray()
    while len(out) < size:
        out += bytearray(m)
        m = hashlib.md5(m).digest()
    return out

total_n = 0
n = 0
verified = 0
verified_d = 0
start = time.time()
prev = start
w = 0

while True:
    # pick a number of blocks to work on
    cur_n_blocks = random.randint(1, max_b)
    # pick an offset (nr) in the device
    offset = random.randint(0, dev_size - blocksize * cur_n_blocks) & ~(blocksize - 1)
    nr = offset // blocksize

    # have all 'cur_n_blocks' starting at 'nr' been written to?
    b = []
    has_none = False
    for i in range(0, cur_n_blocks):
        if seen[nr + i] == None:
            has_none = True
            break

    if has_none == False:  # yes(!)
        # verify
        for i in range(0, cur_n_blocks):
            b.append(gen_block(blocksize, offset + i * blocksize, seen[nr + i]))
            if seen[nr + i] == duplicate:
                verified_d += 1

        # read from disk
        data = os.pread(fd, blocksize * cur_n_blocks, offset)

        for i in range(0, cur_n_blocks):
            cur_b_offset = i * blocksize 
            assert(data[cur_b_offset:cur_b_offset+blocksize] == b[i])

            verified += 1

    else:
        w += cur_n_blocks

    # update blocks with new data
    b = bytearray()
    for i in range(0, cur_n_blocks):
        # choose new seed. if not duplicate (e.g. not de-dubeable), use a random
        new_seen = duplicate if random.randint(0, 100) > unique_perc else random.randint(0, 65535)
        # remember the seed
        seen[nr + i] = new_seen
        # generate & add block of semi(!)-random data
        b += gen_block(blocksize, offset + i * blocksize, seen[nr + i])
    os.pwrite(fd, b, offset)
    os.fdatasync(fd)

    n += 1
    total_n += cur_n_blocks

    now = time.time()
    if now - prev >= 1:
        print(f'total: {n}, n/s: {int(n / (now - start))}, avg block count per iteration: {total_n / n:.2f}, percent done: {w * 100 / n_blocks:.2f}, n verified: {verified}/{verified_d}')
        prev = now
