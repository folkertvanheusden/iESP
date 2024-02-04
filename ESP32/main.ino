// (C) 2023-2024 by Folkert van Heusden
// Released under MIT license

#include <atomic>
#include <csignal>
#include <cstdio>
#include <ESPmDNS.h>
#include <SPI.h>
#include <LittleFS.h>
#include <WiFiManager.h>

#include "backend-sdcard.h"
#include "server.h"
#include "version.h"


std::atomic_bool stop { false };

WiFiManager *wifiManager { nullptr };

void setup()
{
	Serial.begin(115200);
	Serial.setDebugOutput(true);
	WiFi.hostname("iESP");

	Serial.println(F("iESP, (C) 2023-2024 by Folkert van Heusden <mail@vanheusden.com>"));
	Serial.println(F("Compiled on " __DATE__ " " __TIME__));
	Serial.print(F("GIT hash: "));
	Serial.println(version_str);

	wifiManager = new WiFiManager();
	wifiManager->setConfigPortalTimeout(120);
	wifiManager->autoConnect();

	esp_wifi_set_ps(WIFI_PS_NONE);

	if (MDNS.begin("iESP"))
		MDNS.addService("iscsi", "tcp", 3260);
	else
		Serial.println(F("Failed starting mdns responder"));

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
