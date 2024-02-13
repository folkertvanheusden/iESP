// (C) 2023-2024 by Folkert van Heusden
// Released under MIT license

#include <atomic>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <csignal>
#include <cstdio>
#include <esp_wifi.h>
#include <ESPmDNS.h>
#include <ETH.h>
// M.A.X.X:
#include <LittleFS.h>
#include <configure.h>
#include <wifi.h>

#include "backend-sdcard.h"
#include "com-sockets.h"
#include "server.h"
#include "version.h"


bool ota_update = false;
std::atomic_bool stop { false };
char name[16] { 0 };
backend_sdcard  *bs { nullptr };
scsi *scsi_dev { nullptr };

DynamicJsonDocument cfg(4096);
#define IESP_CFG_FILE "/cfg-iESP.json"
AsyncWebServer web_server(80);

TaskHandle_t task2;

bool load_configuration() {
	File data_file = LittleFS.open(IESP_CFG_FILE, "r");
	if (!data_file)
		return false;

	auto error = deserializeJson(cfg, data_file);
	data_file.close();
	if (error)  // this should not happen
		return false;

	return true;
}

bool save_configuration()
{
	File data_file = LittleFS.open(IESP_CFG_FILE, "w");
	if (!data_file)
		return false;

	serializeJson(cfg, data_file);
	data_file.close();

	return true;
}

void setup_web_server() {
	web_server.on("/", HTTP_GET, [] (AsyncWebServerRequest *request) {
		Serial.println(request->url().c_str());

		request->redirect("/index.html");
	});

	web_server.on("/index.html", HTTP_GET, [] (AsyncWebServerRequest *request) {
		Serial.println(request->url().c_str());

		AsyncResponseStream *response = request->beginResponseStream("text/html");

		response->printf("<!DOCTYPE html><html><head><link rel=\"stylesheet\" href=\"/stylesheet.css\"><title>iESP (%s)</title><body><h1>iESP (%s)</h1>"
			"<h2>website</h2>"
			"<p><a href=\"https://vanheusden.com/electronics/iESP/\">https://vanheusden.com/electronics/iESP/</a></p>"
			"<h2>copyrights</h2>"
			"<p>(c) 2023-2024 by Folkert van Heusden <mail@vanheusden.com></p></body></html>", name, name);

		request->send(response);
	});

	web_server.begin();
}

void enable_OTA() {
	ArduinoOTA.setPort(3232);
	ArduinoOTA.setHostname(name);
	ArduinoOTA.setPassword("iojasdsjiasd");

	ArduinoOTA.onStart([]() {
			Serial.println(F("OTA start"));
			ota_update = true;
			stop = true;
			LittleFS.end();
			});
	ArduinoOTA.onEnd([]() {
			Serial.println(F("OTA end"));
			});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
			Serial.printf("OTA progress: %u%%\r", progress * 100 / total);
			});
	ArduinoOTA.onError([](ota_error_t error) {
			Serial.printf("Error[%u]: ", error);
			if (error == OTA_AUTH_ERROR) Serial.println(F("auth failed"));
			else if (error == OTA_BEGIN_ERROR) Serial.println(F("begin failed"));
			else if (error == OTA_CONNECT_ERROR) Serial.println(F("connect failed"));
			else if (error == OTA_RECEIVE_ERROR) Serial.println(F("receive failed"));
			else if (error == OTA_END_ERROR) Serial.println(F("end failed"));
			});
	ArduinoOTA.begin();

	Serial.println(F("OTA ready"));
}

bool progress_indicator(const int nr, const int mx, const std::string & which) {
	printf("%3.2f%%: %s\r\n", nr * 100. / mx, which.c_str());

	return true;
}

void setup_wifi() {
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

void loopw(void *) {
	Serial.println(F("Thread started"));

	for(;;) {
		ArduinoOTA.handle();

		vTaskDelay(100 / portTICK_PERIOD_MS);
	}
}

void setup() {
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

	if (load_configuration() == false)
		Serial.println(F("Failed to load configuration, using defaults!"));

	bs = new backend_sdcard();
	scsi_dev = new scsi(bs);

	set_hostname(name);

	WiFi.onEvent(WiFiEvent);
	ETH.begin(1, 16, 23, 18, ETH_PHY_LAN8720);

	if (MDNS.begin(name))
		MDNS.addService("iscsi", "tcp", 3260);
	else
		Serial.println(F("Failed starting mdns responder"));

	Serial.print("Waiting for Ethernet: ");
	while(eth_connected == false) {
		delay(200);
		Serial.print(".");
	}

//	setup_wifi();
//	esp_wifi_set_ps(WIFI_PS_NONE);

	enable_OTA();

	setup_web_server();

	xTaskCreate(loopw, /* Function to implement the task */
			"loop2", /* Name of the task */
			10000,  /* Stack size in words */
			nullptr,  /* Task input parameter */
			127,  /* Priority of the task */
			&task2  /* Task handle. */
		   );
}

void loop()
{
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

	if (ota_update) {
		Serial.println(F("Halting for OTA update"));

		for(;;)
			delay(1000);
	}
}
