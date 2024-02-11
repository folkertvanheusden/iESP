// (C) 2023-2024 by Folkert van Heusden
// Released under MIT license

#include <atomic>
#include <csignal>
#include <cstdio>
#include <esp_wifi.h>
#include <ESP32-ENC28J60.h>
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
char name[16] { 0 };
backend_sdcard  *bs { nullptr };
scsi *scsi_dev { nullptr };

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

#define E_MISO_GPIO 12
#define E_MOSI_GPIO 13
#define E_SCLK_GPIO 14
#define E_CS_GPIO   15
#define E_INT_GPIO  4

volatile bool eth_connected = false;

void WiFiEvent(WiFiEvent_t event)
{
	switch (event) {
		case ARDUINO_EVENT_ETH_START:
			Serial.println("ETH Started");
			//set eth hostname here
			ETH.setHostname(name);
			break;
		case ARDUINO_EVENT_ETH_CONNECTED:
			Serial.println("ETH Connected");
			break;
		case ARDUINO_EVENT_ETH_GOT_IP:
			Serial.print("ETH MAC: ");
			Serial.print(ETH.macAddress());
			Serial.print(", IPv4: ");
			Serial.print(ETH.localIP());
			if (ETH.fullDuplex())
				Serial.print(", FULL_DUPLEX");
			Serial.print(", ");
			Serial.print(ETH.linkSpeed());
			Serial.println("Mbps");
			eth_connected = true;
			break;
		case ARDUINO_EVENT_ETH_DISCONNECTED:
			Serial.println("ETH Disconnected");
			eth_connected = false;
			break;
		case ARDUINO_EVENT_ETH_STOP:
			Serial.println("ETH Stopped");
			eth_connected = false;
			break;
		default:
			Serial.printf("Unknown/unexpected event %d\r\n", event);
			break;
	}
}

void setup()
{
	Serial.begin(115200);
	while(!Serial)
		yield();
	Serial.setDebugOutput(true);

	uint8_t chipid[6] { };
	esp_read_mac(chipid, ESP_MAC_WIFI_STA);
	snprintf(name, sizeof name, "iESP-%02x%02x%02x%02x", chipid[2], chipid[3], chipid[4], chipid[5]);

	Serial.println(F("iESP, (C) 2023-2024 by Folkert van Heusden <mail@vanheusden.com>"));
	Serial.println(F("Compiled on " __DATE__ " " __TIME__));
	Serial.print(F("GIT hash: "));
	Serial.println(version_str);
	Serial.print(F("System name: "));
	Serial.println(name);

	auto reset_reason = esp_reset_reason();
	if (reset_reason != ESP_RST_POWERON)
		Serial.printf("Reset reason: %d\r\n", reset_reason);

	bs = new backend_sdcard();
	scsi_dev = new scsi(bs);

//	setup_wifi();
//	esp_wifi_set_ps(WIFI_PS_NONE);

	WiFi.onEvent(WiFiEvent);
	ETH.begin(E_MISO_GPIO, E_MOSI_GPIO, E_SCLK_GPIO, E_CS_GPIO, E_INT_GPIO, 10, 1);

	if (MDNS.begin(name))
		MDNS.addService("iscsi", "tcp", 3260);
	else
		Serial.println(F("Failed starting mdns responder"));

	while(eth_connected == false) {
		delay(200);
		Serial.print(".");
	}
}

void loop()
{
	auto ip = ETH.localIP();
	char buffer[16];
	snprintf(buffer, sizeof buffer, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

	Serial.print(F("Will listen on (in a bit): "));
	Serial.println(buffer);

	com_sockets c(buffer, 3260, &stop);
	if (c.begin() == false)
		Serial.println(F("Failed to initialize communication layer!"));

	server s(scsi_dev, &c);
	Serial.println(F("Go!"));
	s.handler();
}
