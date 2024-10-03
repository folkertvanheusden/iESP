what it is
----------
iESP is an iSCSI target that was originally designed for the ESP32 and Teensy 4.1 microcontroller. This allows you to boot your VMWare cluster from an SD-card :-)
Since then it was ported to Linux, \*BSD and Microsoft Windows.


requirements
------------
* a Linux/\*BSD/Windows system or a supported microcontroller with an SD-card reader connected to it.
* platformio (for microcontrollers)
* cmake (for the other systems)


compiling
---------
For the ESP32/Teensy4.1:
* cd microcontrollers
* pio run -t upload

For Linux/FreeBSD:
* mkdir build
* cd build
* cmake ..
* make

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
* .1.3.6.1.4.1.2021.4.11.0      - free RAM (kB heap space)       (not in Posix version)
* .1.3.6.1.4.1.2021.9.1.9.1     - disk free estimate (will only work when using TRIM/UNMAP/DISCARD)


test tools
----------
* test-blockdevice.py  tests if what is written, is readable later on. this test overwrites the contents of a device!
* block-speed-randread.py  measures the bandwidth/iops for random reads. use plot.sh to create png-files of the output.


disclaimer
----------
Things are not stable/reliable yet for microcontrollers: it may destroy the contents of your SD-card.
On other systems it should run fine. Tested with "test-blockdevice.py" and 'FSX' by Apple (the rust version from https://github.com/asomers/fsx-rs ).


license
-------
It is licensed under the MIT license.
Written by Folkert van Heusden <mail@vanheusden.com> in 2023/2024.
