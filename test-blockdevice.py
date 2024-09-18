#! /usr/bin/python3

import getopt
import hashlib
import os
import random
import sys
import time

##### DO NOT RUN THIS ON A DEVICE WITH DATA! IT GETS ERASED! ######

hash_algo = hashlib.sha3_512
fast_random = False
dev = None
blocksize = 4096
max_b = 16
unique_perc = 51

def help():
    print(f'Usage: {sys.argv[0]} ...arguments...')
    print('-d dev:               block device')
    print('-b blocksize:         e.g. 512 or 4096')
    print('-m maxblockcount:     maximum number of blocks to write in one go')
    print('-u unique-percentage: for testing de-duplication devices')
    print('-f fast random:       used for generating non-dedupable data')
    print()
    print(' ##### DO NOT RUN THIS ON A DEVICE WITH DATA! IT GETS ERASED! ###### ')
    print()

try:
    opts, args = getopt.getopt(sys.argv[1:], 'd:b:m:u:fh')
except getopt.GetoptError as err:
    print(err)
    help()
    sys.exit(2)

for o, a in opts:
    if o == '-d':
        dev = a
    elif o == '-b':
        blocksize = int(a)
    elif o == '-m':
        max_b = int(a)
    elif o == '-u':
        unique_perc = int(a)
    elif o == '-f':
        fast_random = True
    elif o == '-h':
        help()
        sys.exit(0)

if dev == None:
    help()
    sys.exit(1)

random.seed()

seed = int(time.time())
fd = os.open(dev, os.O_RDWR)
dev_size = os.lseek(fd, 0, os.SEEK_END)

n_blocks = dev_size // blocksize
if dev_size % blocksize:
    print(f'Note: disk is not a multiple of {blocksize} in size ({dev_size})!')
seen = [ None ] * n_blocks

duplicate = 0xffffffff

rnd = random.Random()

def gen_block(size, offset, seed2):
    if seed2 == duplicate:
        seed_data = seed.to_bytes(8, 'big')
    else:
        seed_data = offset.to_bytes(8, 'big') + seed.to_bytes(8, 'big') + seed2.to_bytes(4, 'big')
    if fast_random:
        rnd.seed(seed_data)
        out = rnd.randbytes(size)
    else:
        out = bytearray()
        m = hash_algo(seed_data).digest()
        while len(out) < size:
            out += bytearray(m)
            m = hash_algo(m).digest()
    return out

total_n = 0
n = 0
verified = 0
verified_d = 0
start = time.time()
prev = start
w = 0
read_error_count = 0
write_error_count = 0

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
        try:
            data = os.pread(fd, blocksize * cur_n_blocks, offset)

            for i in range(0, cur_n_blocks):
                cur_b_offset = i * blocksize 
                assert(data[cur_b_offset:cur_b_offset+blocksize] == b[i])
                verified += 1

        except OSError as e:
            print(f'Read error: {e} at {offset} ({len(b)} bytes)', offset/blocksize)
            read_error_count += 1

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
    try:
        os.pwrite(fd, b, offset)
        os.fdatasync(fd)
    except OSError as e:
        print(f'Write error: {e} at {offset} ({len(b)} bytes)', offset/blocksize)
        write_error_count += 1

    n += 1
    total_n += cur_n_blocks

    now = time.time()
    if now - prev >= 1:
        print(f'total: {n}, n/s: {int(n / (now - start))}, avg block count per iteration: {total_n / n:.2f}, percent done: {w * 100 / n_blocks:.2f}, n verified: {verified}/{verified_d}, write errors: {write_error_count}')
        prev = now
