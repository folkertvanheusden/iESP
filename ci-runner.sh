#! /bin/sh -e

P=/tmp/iesp.ci
rm -rf $P

git clone . $P

cat > $P/microcontrollers/data/cfg-iESP.json <<EOF
{
	"wifi" : [
		{ "ssid": "www.vanheusden.com", "psk": "Ditiseentest31415926" },
		{ "ssid": "NurdSpace", "psk": "harkharkhark" }
	],
	"syslog-host" : "10.208.3.58",
	"log-level": "debug",
	"trim-level": 2,
	"eth-wait-time": 1,
	"update-df-interval": 5,
	"led-green": 17,
	"led-yellow": 21,
	"led-red": 22,
	"SD_MISO": 19,
	"SD_MOSI": 23,
	"SD_SCLK": 18,
	"SD_CS": 5
}
EOF

# C++
(cd $P ; (mkdir build ; cd build ; cmake .. ; make -j6) )

# Arduino prepare
mkdir -p $P/microcontrollers/.pio
cp -a microcontrollers/.pio/libdeps $P/microcontrollers/.pio

# Arduino: Wemos32
(cd $P/microcontrollers/ ; pio run -e ESP32-wemos)

# Arduino: Teensy4.1
(cd $P/microcontrollers/ ; pio run -e Teensy4_1)
