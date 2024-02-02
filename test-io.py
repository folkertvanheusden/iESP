#! /usr/bin/python3

# https://github.com/python-scsi/python-scsi/

from pyscsi.pyscsi.scsi_cdb_read16 import Read16
from pyscsi.pyscsi.scsi_cdb_write16 import Write16
from pyscsi.pyscsi.scsi_enum_command import sbc
from pyscsi.pyscsi.scsi import SCSI
from pyscsi.utils import init_device

# device = init_device('iscsi://192.168.64.206/test/0')
device = init_device('iscsi://192.168.65.245/test/1')
# device = init_device('iscsi://192.168.65.238/target1/0')
bs = 4096
count = 256
step_start = 1
errors = False
with SCSI(device, blocksize=bs) as d:
    d.device.opcodes = sbc

    for step in range(step_start, count):
        print(f'Step: {step}')
        print('  write')
        data = bytearray([ 0 ] * bs * step)
        for i in range(0, count, step):
            data[0] = i
            w = d.write16(i, step, data).dataout

        print('  read & verify')
        for i in range(0, count, step):
            r = d.read16(i, step).datain

            if r[0] != i:
                print(r[t], i, r[t] == i)
                errors = True

if not errors:
    print('All good!')
