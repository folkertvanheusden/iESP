#! /usr/bin/python3

# https://github.com/python-scsi/python-scsi/

from pyscsi.pyscsi.scsi import SCSI
from pyscsi.utils import init_device

device = init_device('iscsi://192.168.64.206/test/0')
# device = init_device('iscsi://192.168.65.245/test2/0')
# device = init_device('iscsi://192.168.65.238/target1/0')
bs = 4096
count = 256
step_start = 1

with SCSI(device, blocksize=bs) as s:
    for step in range(step_start, count):
        print(f'Step: {step}')
        print('  write')
        for i in range(0, count, step):
            data = bytearray([ i ] * bs)
            w = s.write16(i, step, data).dataout

        print('  read & verify')
        for i in range(0, count, step):
            r = s.read16(i, step).datain

            for t in range(0, count):
                if r[t] != i:
                    print(r[t], i, r[t] == i)
