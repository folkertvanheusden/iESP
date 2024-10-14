#! /bin/sh

# prereq.:
# ramdisk under /mnt
# truncate -s 900M /mnt/test.dat
# apt install gcovr gcov
# pip3 install python3-iscsi
# libiscsi installed with test-tools inder /usr/local/libiscsi

TMPDIR=/mnt
TMPIMG=$TMPDIR/test.dat
PIDFILE=$TMPDIR/iesp.pid
LOGFILE=/tmp/iesp.log
DIGESTDISABLE=-D

rm -rf ./build/*
(cd build && CXXFLAGS=-coverage LDFLAGS=-coverage cmake -D CMAKE_BUILD_TYPE=Debug .. && make -j)

./build/iesp -h
if [ $? -ne 0 ] ; then
	echo 'iesp -h failed'
	exit 1
fi

rm -f $LOGFILE
./build/iesp -b file -d $TMPIMG -L error,debug -l $LOGFILE -P $PIDFILE -f -S 1610 $DIGESTDISABLE
LINES=`snmpwalk -c public -v2c localhost:1610 -O n .iso 2> /dev/null | grep -v 'End of MIB' | wc -l`

cat <<EOF | /usr/bin/python3
from pyscsi.pyscsi.scsi import SCSI
from pyscsi.utils import init_device

with SCSI(init_device('iscsi://localhost/test/1'), blocksize=512) as s:
    r = s.read16(1, 8192,).datain
    assert len(r) == 8192 * 512

data = bytearray(4096)
with SCSI(init_device('iscsi://localhost/test/1'), blocksize=4096) as s:
    w = s.writesame16(1024, 27, data)
    print(w.result)

data = bytearray(4096 * 8192)
with SCSI(init_device('iscsi://localhost/test/1'), blocksize=4096) as s:
    w = s.write16(1, 8192, data)
    print(w.result)
EOF

if [ $? -ne 0 ] ; then
	echo 'reading 4 MB failed'
	exit 1
fi

if [ $LINES -eq 26 ] ; then
	PID=`cat $PIDFILE`
	(cd $TMPDIR ; /usr/local/libiscsi/bin/iscsi-test-cu -d -S -x -s iscsi://localhost/test/1 > /dev/null)

	sleep 1
	while true
	do
		kill -TERM $PID
		if [ $? -ne 0 ] ; then
			break
		fi
		sleep 1
	done

	SOURCES=`pwd`
	CC=$TMPDIR/coverage1.info
	echo "cd $TMPDIR ; geninfo $SOURCES/build/CMakeFiles/iesp.dir/ -b $SOURCES/ -o $CC --branch-coverage && genhtml $CC -o temp"
	(cd $TMPDIR ; geninfo $SOURCES/build/CMakeFiles/iesp.dir/ -b $SOURCES/ -o $CC --branch-coverage --rc geninfo_unexecuted_blocks=1 --ignore-errors mismatch --ignore-errors gcov && genhtml $CC -o temp)

	TJSON=$TMPDIR/test.json
	gcovr --json-summary $TJSON
else
	echo SNMP result line-count mismatch
	exit 1
fi

exit 0
