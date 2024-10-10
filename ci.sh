#! /bin/sh

ssh -CAYf 172.29.0.132 /home/folkert/CC.work/update-iESP.sh

F=/tmp/iesp-ci.log
rm -f $F
./ci-runner.sh > $F 2>&1

if [ $? -ne 0 ] ; then
	cat $F

	echo ' *** BUILD FAILED ***'
else
	echo 'Build OK'
fi
