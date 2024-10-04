#! /bin/sh

F=/tmp/iesp-ci.log
rm -f $F
./ci-runner.sh > $F 2>&1

if [ $? -ne 0 ] ; then
	cat $F

	echo ' *** BUILD FAILED ***'
else
	echo 'Build OK'
fi
