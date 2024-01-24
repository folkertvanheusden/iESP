// (C) 2023-2024 by Folkert van Heusden
// Released under MIT license

#include <csignal>
#include <cstdio>
#include <SPI.h>
#include <LittleFS.h>
#include <WiFiManager.h>

#include "backend-sdcard.h"
#include "server.h"


WiFiManager wifiManager;

void setup()
{
	Serial.begin(115200);
	Serial.println(F("iESP, (C) 2023-2024 by Folkert van Heusden <mail@vanheusden.com"));

	wifiManager.setConfigPortalTimeout(120);
	wifiManager.autoConnect();

	Serial.print(F("Will listen on (in a few seconds): "));
	Serial.println(WiFi.localIP());
}

void loop()
{
	backend_sdcard bs;

	auto ip = WiFi.localIP();
	char buffer[16];
	snprintf(buffer, sizeof buffer, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

	server s(&bs, buffer, 3260);
	s.begin();

	s.handler();
}
