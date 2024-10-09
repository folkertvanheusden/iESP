// (C) 2024 by Folkert van Heusden
// Released under MIT license

#include <atomic>
#include <csignal>
#include <cstdio>
#include <SPI.h>
#include <WiFi.h>
#include <hardware/watchdog.h>

#include "backend-sdcard-rp2040w.h"
#include "com-arduino.h"
#include "random.h"
#include "server.h"
#include "version.h"
#include "wifi.h"


std::atomic_bool stop { false   };
com             *c    { nullptr };
backend_sdcard_rp2040w *bs   { nullptr };
scsi            *sd   { nullptr };
server          *s    { nullptr };
iscsi_stats_t    is;
io_stats_t       ios;
volatile bool    wifi_connected { false };
int              led_green  {  17 };
int              led_yellow {  18 };
int              led_red    { -1  };

bool is_network_up()
{
	return wifi_connected;
}

void write_led(const int gpio, const int state) {
	if (gpio != -1)
		digitalWrite(gpio, state);
}

void setup()
{
	Serial.begin(115200);
	while(!Serial)
		delay(100);
	Serial.setDebugOutput(true);

	Serial.println(F("iESP (for RP2040W), (C) 2023-2024 by Folkert van Heusden <mail@vanheusden.com>"));
	Serial.println(F("Compiled on " __DATE__ " " __TIME__));
	Serial.print(F("GIT hash: "));
	Serial.println(version_str);

	Serial.print(F("Free memory at start: "));
	Serial.println(rp2040.getFreeHeap());

	if (rp2040.isPicoW() == false)
		Serial.println(F("This is NOT a Pico-W, this program will fail!"));

	if (watchdog_caused_reboot())
		Serial.println(F("Rebooted by watchdog"));

	if (led_green != -1)
		pinMode(led_green, OUTPUT);
	if (led_yellow != -1)
		pinMode(led_yellow, OUTPUT);
	if (led_red != -1)
		pinMode(led_red, OUTPUT);

	try {
		c = new com_arduino(3260);
		wifi_connected = c->begin();
		if (wifi_connected == false)
			Serial.println(F("Failed to initialize com-layer"));

		init_my_getrandom();

		Serial.println(F("Init SD card"));
		bs = new backend_sdcard_rp2040w(led_green, led_yellow);
		bs->begin();

		Serial.println(F("Create SCSI instance"));
		sd = new scsi(bs, 1, &ios);

		Serial.println(F("Instantiate iSCSI server"));
		s = new server(sd, c, &is, "test", false);

		Serial.print(F("Free memory after full init: "));
		Serial.println(rp2040.getFreeHeap());

		Serial.print(F("Enable watchdog"));
		watchdog_enable(2500, 1);  // 2.5s

		Serial.println(F("Setup step finished"));
	}
	catch(...) {
		Serial.println(F("An exception occured during init"));
	}
}

void loop()
{
	Serial.print(millis());
	Serial.println(F(" Go!"));

	try {
		s->handler();
	}
	catch(...) {
		Serial.println(F("An exception occured during run-time"));
	}
}
