#! /bin/sh

for i in $*
do
	A=`echo $i | cut -d : -f 1`
	/home/folkert/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-addr2line -e .pio/build/waveshare_s3_eth/firmware.elf $A
done
