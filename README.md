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
On the ESP32 it uses the connected SD-card. Make sure it is formatted in 'exfat' format. Create a test.dat file on the SD-card of the size you want your iSCSI target to be. The ESP32 version uses 'WifiManager': after the first powerup you need to configure an access point via the temporary accesspoint.

On Linux/FreeBSD, it assumes you have a test.dat file (in the same directory) of appropriate size. Note that you need to adjust the IP-address in main.cpp for it to work.


disclaimer
----------
Things are not stable/reliable yet: it may destroy the contents of your SD-card.


license
-------
It is licensed under the MIT license.
Written by Folkert van Heusden <mail@vanheusden.com> in 2023/2024.
