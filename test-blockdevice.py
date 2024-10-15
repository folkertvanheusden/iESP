#! /usr/bin/python3

import getopt
import hashlib
import os
import random
import sys
import threading
import time

from ctypes import *
libc = cdll.LoadLibrary("libc.so.6")

##### DO NOT RUN THIS ON A DEVICE WITH DATA! IT GETS ERASED! ######

hash_algo = hashlib.sha3_512
fast_random = False
dev = None
blocksize = 4096
max_b = 16
unique_perc = 51
trim_perc = 0
n_threads = 2
stop_at_100 = False

def cmdline_help():
    print(f'Usage: {sys.argv[0]} ...arguments...')
    print('-d dev:               block device')
    print('-b blocksize:         e.g. 512 or 4096')
    print('-m maxblockcount:     maximum number of blocks to write in one go')
    print('-u unique-percentage: for testing de-duplication devices')
    print('-f fast random:       used for generating non-dedupable data')
    print('-n thread count:      number of parallel threads. run this with PYTHON_GIL=0 (python 3.13 and more recent)')
    print('-T trim-percentage:   how much to apply "trim"')
    print('-t                    terminate when aproximately 100% (at least) is tested')
    print()
    print(' ##### DO NOT RUN THIS ON A DEVICE WITH DATA! IT GETS ERASED! ###### ')
    print()

try:
    opts, args = getopt.getopt(sys.argv[1:], 'd:b:m:u:fn:T:th')
except getopt.GetoptError as err:
    print(err)
    cmdline_help()
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
    elif o == '-n':
        n_threads = int(a)
    elif o == '-T':
        trim_perc = int(a)
    elif o == '-t':
        stop_at_100 = True
    elif o == '-h':
        cmdline_help()
        sys.exit(0)

if dev == None:
    cmdline_help()
    sys.exit(1)

random.seed()

seed = int(time.time())
fd = os.open(dev, os.O_RDWR)
dev_size = os.lseek(fd, 0, os.SEEK_END)

print(f'Device size: {dev_size} bytes or {dev_size // 1024 // 1024 // 1024} GB')

n_blocks = dev_size // blocksize
if dev_size % blocksize:
    print(f'Note: disk is not a multiple of {blocksize} in size ({dev_size})!')
seen = [ None ] * n_blocks

apply_trim = 0xfffffffe
duplicate = 0xffffffff

random.seed()  # different from random.Random

def gen_block(size, offset, seed2):
    if seed2 == duplicate:
        seed_data = seed.to_bytes(8, 'big')
    elif seed2 == apply_trim:
        return bytes(size)
    else:
        seed_data = offset.to_bytes(8, 'big') + seed.to_bytes(8, 'big') + seed2.to_bytes(4, 'big')
    if fast_random:
        rnd = random.Random()
        rnd.seed(seed_data)
        out = rnd.randbytes(size)
    else:
        out = bytearray()
        m = hash_algo(seed_data).digest()
        while len(out) < size:
            out += bytearray(m)
            m = hash_algo(m).digest()
    return out

start = time.time()
total_n = 0
n = 0
verified = 0
verified_d = 0
verified_t = 0
read_error_count = 0
write_error_count = 0
data_total = 0
failure_count = 0

lock = threading.Lock()
ranges = []

def do(show_stats):
    global total_n
    global n
    global verified
    global verified_d
    global verified_t
    global read_error_count
    global write_error_count
    global data_total
    global failure_count

    n_failed = 0

    prev = start

    while True:
        ok = True

        lock.acquire()
        while True:
            # pick a number of blocks to work on
            cur_n_blocks = random.randint(1, max_b)
            # pick an offset (nr) in the device
            offset = random.randint(0, dev_size - blocksize * cur_n_blocks) & ~(blocksize - 1)
            nr = offset // blocksize
            # in use?
            in_use = False
            cur_range = set(range(nr, nr + cur_n_blocks))
            for r in ranges:
                if len(cur_range.intersection(range(r[0], r[1]))) > 0:
                    in_use = True
                    break
            if in_use == False:
                ranges.append((nr, nr + cur_n_blocks))
                break
        lock.release()

        # have all 'cur_n_blocks' starting at 'nr' been written to?
        b = []
        has_none = False
        for i in range(0, cur_n_blocks):
            if seen[nr + i] == None:  # Deze check in de read-from-disk-loop.
                has_none = True
                break

        if has_none == False:  # yes(!)
            # verify: generate byterarray containg what should be on disk
            for i in range(0, cur_n_blocks):
                b.append(gen_block(blocksize, offset + i * blocksize, seen[nr + i]))
                if seen[nr + i] == duplicate:
                    verified_d += 1
                if seen[nr + i] == apply_trim:
                    verified_t += 1

            # read from disk
            try:
                byte_count = blocksize * cur_n_blocks
                os.posix_fadvise(fd, offset, byte_count, os.POSIX_FADV_DONTNEED)
                data = os.pread(fd, byte_count, offset)

                for i in range(0, cur_n_blocks):
                    cur_b_offset = i * blocksize
                    if data[cur_b_offset:cur_b_offset+blocksize] != b[i]:
                        if n_failed < 3:
                            print(f'Sector {cur_n_blocks + i} has unexpected data ({data[cur_b_offset:cur_b_offset+16]}... instead of {b[i][0:16]}...')
                            n_failed += 1
                            if n_failed == 3:
                                print('Stopped outputting errors for this thread')
                        ok = False
                    else:
                        verified += 1

            except OSError as e:
                print(f'Read error: {e} at {offset} ({len(b)} bytes)', offset/blocksize)
                read_error_count += 1
                ok = False

        # update blocks with new data
        b = bytearray()
        for i in range(0, cur_n_blocks):
            # choose new seed. if not duplicate (e.g. not de-dubeable), use a random
            perc = random.randint(0, 100)
            if perc < unique_perc:
                new_seen = random.randint(0, 65535)
            elif perc < unique_perc + trim_perc:
                new_seen = apply_trim
            else:
                new_seen = duplicate
            # remember the seed
            seen[nr + i] = new_seen
            # generate & add block of semi(!)-random data
            b += gen_block(blocksize, offset + i * blocksize, seen[nr + i])
        try:
            os.pwrite(fd, b, offset)
            for i in range(0, cur_n_blocks):
                if seen[nr + i] == apply_trim:
                    # 3 = keep size & punch hole
                    libc.fallocate(fd, c_int(3), c_longlong(offset + i * blocksize), c_longlong(blocksize))
            os.fdatasync(fd)
            os.posix_fadvise(fd, offset, len(b), os.POSIX_FADV_DONTNEED)
        except OSError as e:
            print(f'Write/trim error: {e} at {offset} ({len(b)} bytes)', offset/blocksize)
            write_error_count += 1
            ok = False

        n += 1
        total_n += cur_n_blocks

        data_total += cur_n_blocks * blocksize

        if ok == False:
            failure_count += 1

        if show_stats:
            now = time.time()
            time_diff = now - start
            if now - prev >= 1:
                print(f'total: {n}, n/s: {int(n / time_diff)}, avg blocks per it.: {total_n / n:.2f}, percent done: {verified * 100 / n_blocks:.2f}, verify cnt: {verified}/{verified_d}/{verified_t}, failures: {failure_count}, MB/s: {data_total / time_diff / 1024 / 1024:.2f}')
                prev = now

        lock.acquire()
        for i in range(len(ranges)):
            if ranges[i][0] == nr and ranges[i][1] == nr + cur_n_blocks:
                del ranges[i]
                break
        lock.release()

        if verified >= n_blocks and stop_at_100:
            break

t = []
for i in range(n_threads):
    tcur = threading.Thread(target=do, args=(len(t) == 0,))
    tcur.start()
    t.append(tcur)

for th in t:
    th.join()
