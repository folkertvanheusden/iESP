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

./build/iesp -b file -d $TMPIMG -L warning,warning -l $TMPDIR/iesp.log -D -P $PIDFILE -f
(cd $TMPDIR ; /usr/local/libiscsi/bin/iscsi-test-cu -d -S -x -s iscsi://localhost/test/1 > /dev/null)
kill -TERM `cat $PIDFILE`

SOURCES=`pwd`
CC=$TMPDIR/coverage1.info
(cd $TMPDIR ; geninfo $SOURCES/build/CMakeFiles/iesp.dir/ -b $SOURCES/ -o $CC --branch-coverage && mkdir temp && genhtml $CC -o temp)

TJSON=$TMPDIR/test.json
gcovr --json-summary $TJSON
