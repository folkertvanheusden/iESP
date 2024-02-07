// (C) 2024 by Folkert van Heusden
// Released under MIT license

#include <atomic>
#include <csignal>
#include <cstdio>
#include <SPI.h>
#include <WiFiPicker.h>

#include "backend-sdcard.h"
#include "random.h"
#include "server.h"
#include "version.h"


WiFiPicker *wp { nullptr };

std::atomic_bool stop { false };

void setup()
{
	Serial.begin(115200);
	Serial.setDebugOutput(true);
	WiFi.hostname("iESP");

	init_my_getrandom();

	Serial.println(F("iESP (for RP2040W), (C) 2023-2024 by Folkert van Heusden <mail@vanheusden.com>"));
	Serial.println(F("Compiled on " __DATE__ " " __TIME__));
	Serial.print(F("GIT hash: "));
	Serial.println(version_str);

	wp = new WiFiPicker();
	wp->start();

	Serial.print(F("Will listen on (in a bit): "));
	Serial.println(WiFi.localIP());
}

void loop()
{
	backend_sdcard bs;

	auto ip = WiFi.localIP();
	char buffer[16];
	snprintf(buffer, sizeof buffer, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

	server s(&bs, buffer, 3260);
	Serial.println(F("Go!"));
	s.begin();

	s.handler();
}
