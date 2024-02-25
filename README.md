what it is
----------
iESP is an iSCSI target for the ESP32 microcontroller (altough it runs fine on Linux as well).
In theory this allows you to boot your VMWare cluster from an SD-card.


requirements
------------
* Either a Linux/FreeBSD-box or an ESP32 microcontroller with an SD-card reader connected to it.
* platformio (for ESP32)
* cmake (for Linux/FreeBSD)


compiling
---------
For the ESP32:
* cd ESP32
* pio run -t upload

For Linux/FreeBSD:
* mkdir build
* cd build
* cmake ..
* make


using
-----
On the ESP32 it uses the connected SD-card. Make sure it is formatted in 'exfat' format (because of the file size). Create a test.dat file on the SD-card of the size you want your iSCSI target to be. The ESP32 version needs to be configured first: under ESP32/data there's a file called cfg-iESP.json.example. Rename this to cfg-iESP.json and enter appropriate WiFi settings. Leave "syslog-host" empty to not send error logging to a syslog server.

On Linux/FreeBSD, it assumes you have a test.dat file of appropriate size in the current directory. Run iESP with '-h' to see a list of switches. You probably need to change the listen-address for example.

The ESP32 version listens to SNMP (using the Arduino\_SNMP library).
* .1.3.6.1.4.1.2021.13.15.1.1.3 - number of bytes read
* .1.3.6.1.4.1.2021.13.15.1.1.4 - number of bytes written
* .1.3.6.1.4.1.2021.13.15.1.1.5 - number of reads
* .1.3.6.1.4.1.2021.13.15.1.1.6 - number of writes
* .1.3.6.1.4.1.2021.11.9.0 - CPU usage


disclaimer
----------
Things are not stable/reliable yet: it may destroy the contents of your SD-card.


license
-------
It is licensed under the MIT license.
Written by Folkert van Heusden <mail@vanheusden.com> in 2023/2024.
