#! /usr/bin/python3

# https://github.com/python-scsi/python-scsi/

from pyscsi.pyscsi.scsi_cdb_read16 import Read16
from pyscsi.pyscsi.scsi_cdb_write16 import Write16
from pyscsi.pyscsi.scsi_enum_command import sbc
from pyscsi.pyscsi.scsi import SCSI
from pyscsi.utils import init_device

# device = init_device('iscsi://192.168.64.206/test/1')
device = init_device('iscsi://192.168.65.245/test/1')
# device = init_device('iscsi://192.168.65.238/target1/1')
bs = 4096
count = 256
step_start = 3
errors = False
with SCSI(device, blocksize=bs) as d:
    d.device.opcodes = sbc

    for step in range(step_start, count):
        print(f'Step: {step}')
        print('  write')
        for i in range(0, count, step):
            data = bytearray([ i ] * bs * step)
            w = d.write16(i, step, data).dataout

        print('  read & verify')
        for i in range(0, count, step):
            r = d.read16(i, step).datain

            data_size = step * bs
            if len(r) == data_size:
                cur_errors = False
                for t in range(step):
                    o = t * bs
                    if r[o] != i:
                        print(f'Offset in returned data: {o}, value read: {r[o]}, expected value: {i}, equal: {r[o] == i}')
                        cur_errors = errors = True
                if cur_errors:
                    for t in range(data_size):
                        if r[t] != i:
                            print(f'First wrong value is at offset {t}')
                            break
            else:
                print(f'Short read, expected {step * bs} bytes, got {len(r)}')
        break

if not errors:
    print('All good!')
