#! /bin/sh

# prereq.:
# ramdisk under /mnt
# apt install gcovr gcov
# libiscsi installed with test-tools inder /usr/local/libiscsi

TMPDIR=/mnt
TMPIMG=$TMPDIR/test.dat
PIDFILE=$TMPDIR/iesp.pid

rm -rf ./build/*
(cd build && CXXFLAGS=-coverage LDFLAGS=-coverage cmake .. && make -j)

truncate -s 970M $TMPIMG

./build/iesp -b file -d $TMPIMG -L warning,warning -l $TMPDIR/iesp.log -D -P $PIDFILE -f -S 1610
snmpwalk -c public -v 1 localhost:1610
PID=`cat $PIDFILE`
(cd $TMPDIR ; /usr/local/libiscsi/bin/iscsi-test-cu -d -S -x -s iscsi://localhost/test/1 > /dev/null)
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
(cd $TMPDIR ; geninfo $SOURCES/build/CMakeFiles/iesp.dir/ -b $SOURCES/ -o $CC --branch-coverage --rc geninfo_unexecuted_blocks=1 --ignore-errors mismatch && genhtml $CC -o temp)

TJSON=$TMPDIR/test.json
gcovr --json-summary $TJSON
