// (C) 2024 by Folkert van Heusden
// Released under MIT license

#include <atomic>
#include <csignal>
#include <cstdio>
#include <SPI.h>
#include <WiFi.h>

#include "backend-sdcard.h"
#include "com-arduino.h"
#include "random.h"
#include "server.h"
#include "version.h"
#include "wifi.h"


std::atomic_bool stop { false   };
com             *c    { nullptr };
backend_sdcard  *bs   { nullptr };
server          *s    { nullptr };

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

	try {
		c = new com_arduino(3260);
		if (c->begin() == false)
			Serial.println(F("Failed to initialize com-layer"));

		init_my_getrandom();

		Serial.println(F("Init SD card"));
		bs = new backend_sdcard();

		Serial.println(F("Instantiate iSCSI server"));
		s = new server(bs, c);

		Serial.print(F("Free memory after full init: "));
		Serial.println(rp2040.getFreeHeap());

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
