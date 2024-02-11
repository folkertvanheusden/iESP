#! /usr/bin/python3

import os
import random
import sys
import threading
import time

dev = sys.argv[1]
bs_depth = int(sys.argv[2])
jobs_depth = int(sys.argv[3])
duration = int(sys.argv[4])

def do_(duration, bs, results, results_index):
    fd = os.open(dev, os.O_RDONLY)
    dev_size = os.lseek(fd, 0, os.SEEK_END)
    start = time.time()
    iop = 0
    took = 0
    while True:
        offset = random.randint(0, dev_size) & ~(bs - 1)
        os.lseek(fd, offset, os.SEEK_SET)
        os.read(fd, bs)

        iop += 1

        now = time.time()
        if now >= start + duration / 1000.:
            took = now - start
            break

    results[results_index] = iop * duration / (took * 1000.)

    os.close(fd)

fhi = open('plot-iops.dat', 'w')
fhb = open('plot-bw.dat', 'w')

for bs_it in range(0, bs_depth):
    bs = 512 << bs_it

    for jd_it in range(0, jobs_depth):
        temp_jd_nr = jd_it + 1
        threads = [ None ] * temp_jd_nr
        results = [ None ] * temp_jd_nr

        for t in range(0, temp_jd_nr):
            th = threading.Thread(target=do_, args=(duration, bs, results, t))
            th.start()
            threads[t] = th

        total_iops = 0
        for t in range(0, temp_jd_nr):
            threads[t].join()
            total_iops += results[t]

        str_ = f'{bs}\t{temp_jd_nr}\t{total_iops}'
        print(str_)
        fhi.write(f'{str_}\n')

        str_ = f'{bs}\t{temp_jd_nr}\t{total_iops * bs}'
        print(str_)
        fhb.write(f'{str_}\n')

    fhi.write('\n')
    fhb.write('\n')
    print()

fhi.close()
fhb.close()
