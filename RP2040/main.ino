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
backend_sdcard   bs;

void setup()
{
	Serial.begin(115200);
	Serial.setDebugOutput(true);

	init_my_getrandom();

	Serial.println(F("iESP (for RP2040W), (C) 2023-2024 by Folkert van Heusden <mail@vanheusden.com>"));
	Serial.println(F("Compiled on " __DATE__ " " __TIME__));
	Serial.print(F("GIT hash: "));
	Serial.println(version_str);

	c = new com_arduino(3260);
	if (c->begin() == false)
		Serial.println(F("Failed to initialize com-layer"));
}

void loop()
{
	server s(&bs, c);
	Serial.println(F("Go!"));
	s.handler();
}
