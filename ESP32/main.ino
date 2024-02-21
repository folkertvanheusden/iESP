// (C) 2023-2024 by Folkert van Heusden
// Released under MIT license

#include <atomic>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <csignal>
#include <cstdio>
#include <esp_debug_helpers.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include "wifi.h"

#include "backend-sdcard.h"
#include "com-sockets.h"
#include "log.h"
#include "server.h"
#include "version.h"


bool ota_update = false;
std::atomic_bool stop { false };
char name[16] { 0 };
backend_sdcard  *bs { nullptr };
scsi *scsi_dev { nullptr };

DynamicJsonDocument cfg(4096);
#define IESP_CFG_FILE "/cfg-iESP.json"

std::vector<std::pair<std::string, std::string> > wifi_targets;
int trim_level = 0;

TaskHandle_t task2;

void fail_flash() {
	errlog("System cannot continue");

	for(;;) {
		digitalWrite(LED_BUILTIN, HIGH);
		delay(200);
		digitalWrite(LED_BUILTIN, LOW);
		delay(200);
	}
}

bool load_configuration() {
	File data_file = LittleFS.open(IESP_CFG_FILE, "r");
	if (!data_file)
		return false;

	auto error = deserializeJson(cfg, data_file);
	if (error) {  // this should not happen
		data_file.close();
		return false;
	}

	JsonArray w_aps_ar = cfg["wifi"].as<JsonArray>();
	for(JsonObject p: w_aps_ar) {
		auto ssid = p["ssid"];
		auto psk  = p["psk"];

		Serial.print(F("Will search for: "));
		Serial.println(ssid.as<String>());

		wifi_targets.push_back({ ssid, psk });
	}

	syslog_host = cfg["syslog-host"].as<const char *>();
	if (syslog_host.value().empty())
		syslog_host.reset();
	else
		Serial.printf("Syslog host: %s\r\n", syslog_host.value().c_str());

	trim_level = cfg["trim-level"].as<int>();

	data_file.close();

	auto n = wifi_targets.size();
	Serial.printf("Loaded configuration parameters for %zu WiFi access points\r\n", n);
	if (n == 0) {
		Serial.println(F("Cannot continue without WiFi access"));
		fail_flash();
	}

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

void enable_OTA() {
	ArduinoOTA.setPort(3232);
	ArduinoOTA.setHostname(name);
	ArduinoOTA.setPassword("iojasdsjiasd");

	ArduinoOTA.onStart([]() {
			errlog("OTA start\n");
			ota_update = true;
			stop = true;
			LittleFS.end();
			});
	ArduinoOTA.onEnd([]() {
			errlog("OTA end");
			});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
			Serial.printf("OTA progress: %u%%\r", progress * 100 / total);
			});
	ArduinoOTA.onError([](ota_error_t error) {
			errlog("OTA error[%u]: ", error);
			if (error == OTA_AUTH_ERROR) errlog("auth failed");
			else if (error == OTA_BEGIN_ERROR) errlog("begin failed");
			else if (error == OTA_CONNECT_ERROR) errlog("connect failed");
			else if (error == OTA_RECEIVE_ERROR) errlog("receive failed");
			else if (error == OTA_END_ERROR) errlog("end failed");
			});
	ArduinoOTA.begin();

	Serial.println(F("OTA ready"));
}

void WiFiEvent(WiFiEvent_t event)
{
	Serial.print(F("WiFi event: "));

	switch(event) {
		case WIFI_REASON_UNSPECIFIED:
			Serial.println(F("WIFI_REASON_UNSPECIFIED")); break;
		case WIFI_REASON_AUTH_EXPIRE:
			Serial.println(F("WIFI_REASON_AUTH_EXPIRE")); break;
		case WIFI_REASON_AUTH_LEAVE:
			Serial.println(F("WIFI_REASON_AUTH_LEAVE")); break;
		case WIFI_REASON_ASSOC_EXPIRE:
			Serial.println(F("WIFI_REASON_ASSOC_EXPIRE")); break;
		case WIFI_REASON_ASSOC_TOOMANY:
			Serial.println(F("WIFI_REASON_ASSOC_TOOMANY")); break;
		case WIFI_REASON_NOT_AUTHED:
			Serial.println(F("WIFI_REASON_NOT_AUTHED")); break;
		case WIFI_REASON_NOT_ASSOCED:
			Serial.println(F("WIFI_REASON_NOT_ASSOCED")); break;
		case WIFI_REASON_ASSOC_LEAVE:
			Serial.println(F("WIFI_REASON_ASSOC_LEAVE")); break;
		case WIFI_REASON_ASSOC_NOT_AUTHED:
			Serial.println(F("WIFI_REASON_ASSOC_NOT_AUTHED")); break;
		case WIFI_REASON_DISASSOC_PWRCAP_BAD:
			Serial.println(F("WIFI_REASON_DISASSOC_PWRCAP_BAD")); break;
		case WIFI_REASON_DISASSOC_SUPCHAN_BAD:
			Serial.println(F("WIFI_REASON_DISASSOC_SUPCHAN_BAD")); break;
		case WIFI_REASON_IE_INVALID:
			Serial.println(F("WIFI_REASON_IE_INVALID")); break;
		case WIFI_REASON_MIC_FAILURE:
			Serial.println(F("WIFI_REASON_MIC_FAILURE")); break;
		case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
			Serial.println(F("WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT")); break;
		case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
			Serial.println(F("WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT")); break;
		case WIFI_REASON_IE_IN_4WAY_DIFFERS:
			Serial.println(F("WIFI_REASON_IE_IN_4WAY_DIFFERS")); break;
		case WIFI_REASON_GROUP_CIPHER_INVALID:
			Serial.println(F("WIFI_REASON_GROUP_CIPHER_INVALID")); break;
		case WIFI_REASON_PAIRWISE_CIPHER_INVALID:
			Serial.println(F("WIFI_REASON_PAIRWISE_CIPHER_INVALID")); break;
		case WIFI_REASON_AKMP_INVALID:
			Serial.println(F("WIFI_REASON_AKMP_INVALID")); break;
		case WIFI_REASON_UNSUPP_RSN_IE_VERSION:
			Serial.println(F("WIFI_REASON_UNSUPP_RSN_IE_VERSION")); break;
		case WIFI_REASON_INVALID_RSN_IE_CAP:
			Serial.println(F("WIFI_REASON_INVALID_RSN_IE_CAP")); break;
		case WIFI_REASON_802_1X_AUTH_FAILED:
			Serial.println(F("WIFI_REASON_802_1X_AUTH_FAILED")); break;
		case WIFI_REASON_CIPHER_SUITE_REJECTED:
			Serial.println(F("WIFI_REASON_CIPHER_SUITE_REJECTED")); break;
		case WIFI_REASON_BEACON_TIMEOUT:
			Serial.println(F("WIFI_REASON_BEACON_TIMEOUT")); break;
		case WIFI_REASON_NO_AP_FOUND:
			Serial.println(F("WIFI_REASON_NO_AP_FOUND")); break;
		case WIFI_REASON_AUTH_FAIL:
			Serial.println(F("WIFI_REASON_AUTH_FAIL")); break;
		case WIFI_REASON_ASSOC_FAIL:
			Serial.println(F("WIFI_REASON_ASSOC_FAIL")); break;
		case WIFI_REASON_HANDSHAKE_TIMEOUT:
			Serial.println(F("WIFI_REASON_HANDSHAKE_TIMEOUT")); break;
		default:
			Serial.println(event); break;
	}
}

void heap_caps_alloc_failed_hook(size_t requested_size, uint32_t caps, const char *function_name)
{
	errlog("%s was called but failed to allocate %zu bytes with 0x%x capabilities (by %p)", function_name, requested_size, caps, __builtin_return_address(0));

	esp_backtrace_print(25);
}

bool progress_indicator(const int nr, const int mx, const std::string & which) {
	digitalWrite(LED_BUILTIN, HIGH);
	printf("%3.2f%%: %s\r\n", nr * 100. / mx, which.c_str());
	digitalWrite(LED_BUILTIN, LOW);

	return true;
}

void setup_wifi() {
	enable_wifi_debug();

	WiFi.onEvent(WiFiEvent);

	connect_status_t cs = CS_IDLE;
	do {
		start_wifi({ });

		Serial.print(F("Scanning for accesspoints"));
		scan_access_points_start();

		while(scan_access_points_wait() == false) {
			digitalWrite(LED_BUILTIN, HIGH);
			Serial.print(F("."));
			digitalWrite(LED_BUILTIN, LOW);
			delay(100);
		}

		auto available_access_points = scan_access_points_get();
		Serial.printf("Found %zu accesspoints\r\n", available_access_points.size());

		auto state = try_connect_init(wifi_targets, available_access_points, 300, progress_indicator);

		Serial.println(F("Connecting"));
		for(;;) {
			cs = try_connect_tick(state);

			if (cs != CS_IDLE)
				break;

			Serial.print(F("."));
			delay(100);
		}

		// could not connect
	}
	while(cs == CS_FAILURE);
}

void loopw(void *) {
	Serial.println(F("Thread started"));

	for(;;) {
		ArduinoOTA.handle();

		vTaskDelay(100 / portTICK_PERIOD_MS);
	}
}

void ls(fs::FS &fs, const String & name)
{
	Serial.print(F("Directory: "));
	Serial.print(name);

	File dir = fs.open(name);
	if (!dir) {
		Serial.println(F("Can't open"));
		return;
	}

	File file = dir.openNextFile();
	while(file) {
		if (file.isDirectory()) {
			Serial.print("Dir: ");
			Serial.println(file.name());
			ls(fs, name + "/" + file.name());
		}
		else {
			Serial.print("File: ");
			Serial.print(file.name());
			Serial.print(", size: ");
			Serial.println(file.size());
		}

		file = dir.openNextFile();
	}

	dir.close();
}

const char *reset_name(const esp_reset_reason_t rr)
{
	switch(rr) {
		case ESP_RST_UNKNOWN:
			return "Reset reason can not be determined";

		case ESP_RST_POWERON:
			return "Reset due to power-on event";

		case ESP_RST_EXT:
			return "Reset by external pin (not applicable for ESP32)";

		case ESP_RST_SW:
			return "Software reset via esp_restart";

		case ESP_RST_PANIC:
			return "Software reset due to exception/panic";

		case ESP_RST_INT_WDT:
			return "Reset (software or hardware) due to interrupt watchdog";

		case ESP_RST_TASK_WDT:
			return "Reset due to task watchdog";

		case ESP_RST_WDT:
			return "Reset due to other watchdogs";

		case ESP_RST_DEEPSLEEP:
			return "Reset after exiting deep sleep mode";

		case ESP_RST_BROWNOUT:
			return "Brownout reset (software or hardware)";

		case ESP_RST_SDIO:
			return "Reset over SDIO";

//		case ESP_RST_USB:
//			return "Reset by USB peripheral";

//		case ESP_RST_JTAG:
//			return "Reset by JTAG";

		default:
			return "?";
	}

	return nullptr;
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

	pinMode(LED_BUILTIN, OUTPUT);

	if (!LittleFS.begin())
		Serial.println(F("LittleFS.begin() failed"));
	
	if (load_configuration() == false) {
		Serial.println(F("Failed to load configuration, using defaults!"));
		ls(LittleFS, "/");
		fail_flash();
	}

	set_hostname(name);
	setup_wifi();
	init_logger(name);

	bs = new backend_sdcard();
	if (bs->begin() == false) {
		errlog("Failed to load initialize storage backend!");
		fail_flash();
	}

	scsi_dev = new scsi(bs, trim_level);

	auto reset_reason = esp_reset_reason();
	if (reset_reason != ESP_RST_POWERON)
		errlog("Reset reason: %s (%d), software version %s", reset_name(reset_reason), reset_reason, version_str);
	else
		errlog("System (re-)started, software version %s", version_str);

	esp_wifi_set_ps(WIFI_PS_NONE);

	if (MDNS.begin(name))
		MDNS.addService("iscsi", "tcp", 3260);
	else
		errlog("Failed starting mdns responder");

	heap_caps_register_failed_alloc_callback(heap_caps_alloc_failed_hook);

	enable_OTA();

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
		auto ip = WiFi.localIP();
		char buffer[16];
		snprintf(buffer, sizeof buffer, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

		Serial.print(F("Will listen on (in a bit): "));
		Serial.println(buffer);

		com_sockets c(buffer, 3260, &stop);
		if (c.begin() == false) {
			errlog("Failed to initialize communication layer!");
			fail_flash();
		}

		server s(scsi_dev, &c);
		Serial.println(F("Go!"));
		s.handler();
	}

	if (ota_update) {
		errlog("Halting for OTA update");

		for(;;)
			delay(1000);
	}
}
