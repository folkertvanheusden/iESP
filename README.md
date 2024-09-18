what it is
----------
iESP is an iSCSI target for the ESP32 and Teensy 4.1 microcontroller (altough it runs fine on Linux as well).
In theory this allows you to boot your VMWare cluster from an SD-card.


requirements
------------
* Either a Linux/FreeBSD-box or a supported microcontroller (from now on uC) with an SD-card reader connected to it.
* platformio (for microcontrollers)
* cmake (for Linux/FreeBSD)


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


using
-----
On the uC it uses the connected SD-card. Make sure it is formatted in 'exfat' format (because of the file size). Create a test.dat file on the SD-card of the size you want your iSCSI target to be. The uC version needs to be configured first: under microcontrollers/data there's a file called cfg-iESP.json.example. Rename this to cfg-iESP.json and enter e.g. appropriate WiFi settings (if applicable). Leave "syslog-host" empty to not send error logging to a syslog server.

On Linux/FreeBSD, it assumes you have a test.dat file of appropriate size in the current directory. Run iESP with '-h' to see a list of switches. You probably need to change the listen-address for example.

This software has a custom SNMP library.
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


disclaimer
----------
Things are not stable/reliable yet for microcontrollers: it may destroy the contents of your SD-card.


license
-------
It is licensed under the MIT license.
Written by Folkert van Heusden <mail@vanheusden.com> in 2023/2024.
