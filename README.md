what it is
----------
iESP is an iSCSI target that was originally designed for the ESP32 and Teensy 4.1 microcontroller. This allows you to boot your VMWare cluster from an SD-card :-)
Since then it was ported to Linux, \*BSD (tested on FreeBSD) and Microsoft Windows.


requirements
------------
* a Linux/\*BSD/Windows/Apple system with cmake and a C++ compiler

OR

* a supported microcontroller with an SD-card reader connected to it and platformio


compiling
---------
For the ESP32/Teensy4.1:
* cd microcontrollers
* pio run -t upload -e ESP32-wemos  # Wemos32
* pio run -t upload -e Teensy4_1 # Teensy4.1

For the Raspberry Pi Pico (RP2040W):
* cd microcontrollers/RP2040
* * adjust the settings in "wifi.h"
* pio run
* * Manually copy the .pio/build/BUILD_FOR_RP2040W/firmware.uf2 file onto the Pico
* * Optionally connect a green LED to GPIO 17 and a yellow LED to GPIO 18
* * Connect the following pins of your SD card reader to: MISO to GPIO 8, MOSI to GPIO 11, SCK to GPIO 10 and SS/CS to GPIO 12

For Linux/FreeBSD/Apple:
* mkdir build
* cd build
* cmake ..
* make

On Debian systems you can also run the following to create an installable .deb-package file:

* dpkg-buildpackage -us -uc

For RedHat (Fedora etc.):

* rpmbuild -ba iesp.spec

For Windows:
* mkdir buildMingw64 && cd buildMingw64
* cmake -DCMAKE_TOOLCHAIN_FILE=../mingw64.cmake ..

If you copy the result (iesp.exe) to an other windows system, make sure to include:
* libgcc_s_seh-1.dll
* libgomp-1.dll
* libstdc++-6.dll
* libwinpthread-1.dll

When crosscompiling under Debian, they are under /usr/lib/gcc/x86_64-w64-mingw32/13-posix/ and /usr/x86_64-w64-mingw32/lib/.


using
-----
On the microcontroller it uses the connected SD-card. Make sure it is formatted in 'exfat' format (because of the file size). Create a test.dat file on the SD-card of the size you want your iSCSI target to be. The microcontroller version needs to be configured first: under microcontrollers/data there's a file called cfg-iESP.json.example. Rename this to cfg-iESP.json and enter e.g. appropriate WiFi settings (if applicable). Leave "syslog-host" empty to not send error logging to a syslog server.

On non-microcontrollers, run iESP with '-h' to see a list of switches. You probably want to set the backend file/device and to set the listen-address for example. You can also use an NBD-backend, making iESP in an iSCSI-NBD proxy.

This software has a custom SNMP library (SNMP agent).
* .1.3.6.1.2.1.142.1.10.2.1.1   - PDUs received
* .1.3.6.1.2.1.142.1.10.2.1.3   - number of bytes transmitted
* .1.3.6.1.2.1.142.1.10.2.1.4   - number of bytes received
* .1.3.6.1.2.1.142.1.1.1.1.10   - failure count
* .1.3.6.1.2.1.142.1.1.2.1.3    - PDUs with errors
* .1.3.6.1.2.1.142.1.6.2.1.1    - logins
* .1.3.6.1.2.1.142.1.6.3.1.1    - logouts
* .1.3.6.1.4.1.2021.100.2       - software version (not in Posix version)
* .1.3.6.1.4.1.2021.100.3       - build date
* .1.3.6.1.4.1.2021.11.54       - I/O wait in 100ths of a second
* .1.3.6.1.4.1.2021.11.9.0      - CPU usage
* .1.3.6.1.4.1.2021.13.15.1.1.2 - device name
* .1.3.6.1.4.1.2021.13.15.1.1.3 - number of bytes read
* .1.3.6.1.4.1.2021.13.15.1.1.4 - number of bytes written
* .1.3.6.1.4.1.2021.13.15.1.1.5 - number of reads
* .1.3.6.1.4.1.2021.13.15.1.1.6 - number of writes
* .1.3.6.1.4.1.2021.4.11.0      - free RAM (kB heap space, only on microcontrollers)
* .1.3.6.1.4.1.2021.9.1.9.1     - disk free estimate (will only work when using TRIM/UNMAP/DISCARD)


test tools
----------
* test-blockdevice.py  tests if what is written, is readable later on. this test overwrites the contents of a device!
* block-speed-randread.py  measures the bandwidth/iops for random reads. use plot.sh to create png-files of the output.


test methodology
----------------
Tested with "test-blockdevice.py", 'FSX' by Apple (using the rust version from https://github.com/asomers/fsx-rs the original version is at https://github.com/apple/fstools/ ) and the 'test-tool' from libiscsi.

For FSX:
* a disk-image is created on a ramdisk
* that disk-image is given as a backend to iesp
* in a virtual machine, a multipath setup (2 paths) is created
* in the virtual machine, the resulting iSCSI target is mounted under a mountpoint (ext4 filesystem with journal and discard-mountoption)
* 2 instances of FSX(-rs) are started and monitored for error messages
* 1 instance of test-blockdevice.py with 3 threads is started with trim/discard/unmap and deduplication-support set to 81% and trim to 9%

For test-blockdevice.py:
* a disk-image is created on a ramdisk
* that disk-image is given as a backend to iesp
* test-blockdevice.py is ran as: ./test-blockdevice.py -d <devicename> -b 4096 -u 75 -n 6 -T 10 
* any errors? then failed


build status
------------
[POSIX CI](https://github.com/folkertvanheusden/iESP/actions/workflows/posix.yaml/badge.svg)
[Windows CI](https://github.com/folkertvanheusden/iESP/actions/workflows/windows.yaml/badge.svg)
[Microcontrollers CI](https://github.com/folkertvanheusden/iESP/actions/workflows/platformio.yaml/badge.svg)
[MacOS X CI](https://github.com/folkertvanheusden/iESP/actions/workflows/macos.yaml/badge.svg)

[Coverity](https://scan.coverity.com/projects/29725/badge.svg)


license
-------
It is licensed under the MIT license.
Written by Folkert van Heusden <mail@vanheusden.com> in 2023/2024.
