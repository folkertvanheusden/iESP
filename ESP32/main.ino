// (C) 2023-2024 by Folkert van Heusden
// Released under MIT license

#include <atomic>
#include <csignal>
#include <cstdio>
#include <esp_wifi.h>
#include <ESPmDNS.h>
// M.A.X.X:
#include <LittleFS.h>
#include <configure.h>
#include <wifi.h>

#include "backend-sdcard.h"
#include "com-sockets.h"
#include "server.h"
#include "version.h"


std::atomic_bool stop { false };
const char name[] = "iESP";
backend_sdcard  *bs { nullptr };

bool progress_indicator(const int nr, const int mx, const std::string & which) {
	printf("%3.2f%%: %s\r\n", nr * 100. / mx, which.c_str());

	return true;
}

void setup_wifi() {
	set_hostname(name);

	enable_wifi_debug();

	scan_access_points_start();

	if (!LittleFS.begin())
		printf("LittleFS.begin() failed\r\n");

	configure_wifi cw;

	if (cw.is_configured() == false) {
retry:
		Serial.println(F("Cannot connect to WiFi: accesspoint for configuration started"));
		start_wifi(name);  // enable wifi with AP (empty string for no AP)

		cw.configure_aps();
	}
	else {
		Serial.println(F("Connecting to WiFi..."));
		start_wifi("");
	}

	Serial.println(F("Scanning for accesspoints"));
	scan_access_points_start();

	while(scan_access_points_wait() == false)
		delay(100);

	auto available_access_points = scan_access_points_get();

	auto state = try_connect_init(cw.get_targets(), available_access_points, 300, progress_indicator);
	connect_status_t cs = CS_IDLE;

	Serial.println(F("Connecting..."));
	for(;;) {
		cs = try_connect_tick(state);

		if (cs != CS_IDLE)
			break;

		delay(100);
	}

	// could not connect, restart esp
	// you could also re-run the portal
	if (cs == CS_FAILURE) {
		Serial.println(F("Failed to connect"));

		goto retry;
	}
}
void setup()
{
	Serial.begin(115200);
	while(!Serial)
		yield();
	Serial.setDebugOutput(true);

	Serial.println(F("iESP, (C) 2023-2024 by Folkert van Heusden <mail@vanheusden.com>"));
	Serial.println(F("Compiled on " __DATE__ " " __TIME__));
	Serial.print(F("GIT hash: "));
	Serial.println(version_str);

	bs = new backend_sdcard();

	setup_wifi();

	esp_wifi_set_ps(WIFI_PS_NONE);

	if (MDNS.begin(name))
		MDNS.addService("iscsi", "tcp", 3260);
	else
		Serial.println(F("Failed starting mdns responder"));

	Serial.print(F("Will listen on (in a bit): "));
	Serial.println(WiFi.localIP());
}

void loop()
{
	auto ip = WiFi.localIP();
	char buffer[16];
	snprintf(buffer, sizeof buffer, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

	com_sockets c(buffer, 3260, &stop);
	if (c.begin() == false)
		Serial.println(F("Failed to initialize communication layer!"));

	server s(bs, &c);
	Serial.println(F("Go!"));
	s.handler();
}
