// (C) 2023-2024 by Folkert van Heusden
// Released under MIT license

#include <atomic>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <csignal>
#include <cstdio>
#include <esp_debug_helpers.h>
#include <esp_freertos_hooks.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <ESPmDNS.h>
#if defined(WEMOS32)
//
#elif defined(WEMOS32_ETH)
#include <ESP32-ENC28J60.h>
#else
#include <ETH.h>
#endif
#if defined(ESP32)
#include <esp_pthread.h>
#endif
#include <LittleFS.h>
#include <NTP.h>
#include <TM1637.h>

#include "backend-sdcard.h"
#include "com-sockets.h"
#include "log.h"
#include "server.h"
#include "snmp.h"
#include "utils.h"
#include "wifi.h"
#include "version.h"
#include "snmp/snmp.h"


bool ota_update = false;
std::atomic_bool stop { false };
char name[16] { 0 };
backend_sdcard  *bs { nullptr };
scsi *scsi_dev { nullptr };

#if defined(WT_ETH01)
int led_green  = 4;
int led_yellow = 5;
int led_red    = 35;
#elif defined(WEMOS32_ETH) || defined(WEMOS32)
int led_green  = 17;
int led_yellow = 16;
int led_red    = 32;
#else
int led_green  = -1;
int led_yellow = -1;
int led_red    = -1;
#endif

snmp      *snmp_      { nullptr };
snmp_data *snmp_data_ { nullptr };

DynamicJsonDocument cfg(4096);
#define IESP_CFG_FILE "/cfg-iESP.json"

std::vector<std::pair<std::string, std::string> > wifi_targets;
int trim_level = 0;

TaskHandle_t task2;

uint32_t hundredsofasecondcounter = 0;
io_stats_t ios { };
iscsi_stats_t is { };
uint64_t core0_idle = 0;
uint64_t core1_idle = 0;
uint64_t max_idle_ticks = 1855000;
int cpu_usage = 0;
int ram_free_kb = 0;
int eth_wait_seconds = 0;
int update_df_interval = 0;
int percentage_diskspace = 0;

WiFiUDP ntp_udp;
NTP ntp(ntp_udp);

TM1637 TM;

long int draw_status_ts = 0;

void draw_status(const uint32_t v) {
	TM.displayInt(v);

	draw_status_ts = millis();
}

bool idle_task_0() {
	core0_idle++;
	return false;
}

bool idle_task_1() {
	core1_idle++;
	return false;
}

void write_led(const int gpio, const int state) {
	if (gpio != -1)
		digitalWrite(gpio, state);
}

void fail_flash() {
	errlog("System cannot continue");

	write_led(led_green,  LOW);
	write_led(led_yellow, LOW);

	for(;;) {
#ifdef LED_BUILTIN
		digitalWrite(LED_BUILTIN, HIGH);
#endif
		write_led(led_red, HIGH);
		delay(200);
#ifdef LED_BUILTIN
		digitalWrite(LED_BUILTIN, LOW);
#endif
		write_led(led_red, LOW);
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

	eth_wait_seconds = cfg["eth-wait-time"].as<int>();

	update_df_interval = cfg["update-df-interval"].as<int>();

	data_file.close();

	auto n = wifi_targets.size();
	Serial.printf("Loaded configuration parameters for %zu WiFi access points\r\n", n);
	if (n == 0) {
		Serial.println(F("Cannot continue without WiFi access"));
		fail_flash();
	}

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
			write_led(led_red, HIGH);
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

volatile bool eth_connected = false;

bool is_network_up()
{
	return eth_connected;
}

void WiFiEvent(WiFiEvent_t event)
{
	write_led(led_red, HIGH);

	std::string msg = "WiFi event: ";

	switch(event) {
		case WIFI_REASON_UNSPECIFIED:
			msg += "WIFI_REASON_UNSPECIFIED"; break;
		case WIFI_REASON_AUTH_EXPIRE:
			msg += "WIFI_REASON_AUTH_EXPIRE"; break;
		case WIFI_REASON_AUTH_LEAVE:
			msg += "WIFI_REASON_AUTH_LEAVE"; break;
		case WIFI_REASON_ASSOC_EXPIRE:
			msg += "WIFI_REASON_ASSOC_EXPIRE"; break;
		case WIFI_REASON_ASSOC_TOOMANY:
			msg += "WIFI_REASON_ASSOC_TOOMANY"; break;
		case WIFI_REASON_NOT_AUTHED:
			msg += "WIFI_REASON_NOT_AUTHED"; break;
		case WIFI_REASON_NOT_ASSOCED:
			msg += "WIFI_REASON_NOT_ASSOCED"; break;
		case WIFI_REASON_ASSOC_LEAVE:
			msg += "WIFI_REASON_ASSOC_LEAVE"; break;
		case WIFI_REASON_ASSOC_NOT_AUTHED:
			msg += "WIFI_REASON_ASSOC_NOT_AUTHED"; break;
		case WIFI_REASON_DISASSOC_PWRCAP_BAD:
			msg += "WIFI_REASON_DISASSOC_PWRCAP_BAD"; break;
		case WIFI_REASON_DISASSOC_SUPCHAN_BAD:
			msg += "WIFI_REASON_DISASSOC_SUPCHAN_BAD"; break;
		case WIFI_REASON_IE_INVALID:
			msg += "WIFI_REASON_IE_INVALID"; break;
		case WIFI_REASON_MIC_FAILURE:
			msg += "WIFI_REASON_MIC_FAILURE"; break;
		case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
			msg += "WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT"; break;
		case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
			msg += "WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT"; break;
		case WIFI_REASON_IE_IN_4WAY_DIFFERS:
			msg += "WIFI_REASON_IE_IN_4WAY_DIFFERS"; break;
//		case WIFI_REASON_GROUP_CIPHER_INVALID:
//			msg += "WIFI_REASON_GROUP_CIPHER_INVALID"; break;
//		case WIFI_REASON_PAIRWISE_CIPHER_INVALID:
//			msg += "WIFI_REASON_PAIRWISE_CIPHER_INVALID"; break;
//		case WIFI_REASON_AKMP_INVALID:
//			msg += "WIFI_REASON_AKMP_INVALID"; break;
//		case WIFI_REASON_UNSUPP_RSN_IE_VERSION:
//			msg += "WIFI_REASON_UNSUPP_RSN_IE_VERSION"; break;
//		case WIFI_REASON_INVALID_RSN_IE_CAP:
//			msg += "WIFI_REASON_INVALID_RSN_IE_CAP"; break;
		case WIFI_REASON_802_1X_AUTH_FAILED:
			msg += "WIFI_REASON_802_1X_AUTH_FAILED"; break;
		case WIFI_REASON_CIPHER_SUITE_REJECTED:
			msg += "WIFI_REASON_CIPHER_SUITE_REJECTED"; break;
		case WIFI_REASON_BEACON_TIMEOUT:
			msg += "WIFI_REASON_BEACON_TIMEOUT"; break;
		case WIFI_REASON_NO_AP_FOUND:
			msg += "WIFI_REASON_NO_AP_FOUND"; break;
		case WIFI_REASON_AUTH_FAIL:
			msg += "WIFI_REASON_AUTH_FAIL"; break;
		case WIFI_REASON_ASSOC_FAIL:
			msg += "WIFI_REASON_ASSOC_FAIL"; break;
		case WIFI_REASON_HANDSHAKE_TIMEOUT:
			msg += "WIFI_REASON_HANDSHAKE_TIMEOUT"; break;
#if !defined(WEMOS32)
		case ARDUINO_EVENT_ETH_START:
			msg += "ETH Started";
			//set eth hostname here
			ETH.setHostname(name);
			break;
		case ARDUINO_EVENT_ETH_CONNECTED:
			msg += "ETH Connected";
			break;
		case ARDUINO_EVENT_ETH_GOT_IP: {
			auto ip = ETH.localIP();
			msg += "ETH IPv4: ";
			msg += myformat("%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
			if (ETH.fullDuplex())
				msg += ", FULL_DUPLEX";
			msg += ", ";
			msg += myformat("%d", ETH.linkSpeed());
			msg += "Mb";
			eth_connected = true;
			break;
		}
		case ARDUINO_EVENT_ETH_DISCONNECTED:
			msg += "ETH Disconnected";
			eth_connected = false;
			break;
		case ARDUINO_EVENT_ETH_STOP:
			msg += "ETH Stopped";
			eth_connected = false;
			break;
#endif
		default:
			Serial.printf("Unknown/unexpected ETH event %d\r\n", event);
			break;
	}
	errlog(msg.c_str());
	write_led(led_red, LOW);
}

void heap_caps_alloc_failed_hook(size_t requested_size, uint32_t caps, const char *function_name)
{
	write_led(led_red, HIGH);
	errlog("%s was called but failed to allocate %zu bytes with 0x%x capabilities (by %p)", function_name, requested_size, caps, __builtin_return_address(0));

	esp_backtrace_print(25);
	write_led(led_red, LOW);
}

bool progress_indicator(const int nr, const int mx, const std::string & which) {
#ifdef LED_BUILTIN
	digitalWrite(LED_BUILTIN, HIGH);
#endif
	printf("%3.2f%%: %s\r\n", nr * 100. / mx, which.c_str());
#ifdef LED_BUILTIN
	digitalWrite(LED_BUILTIN, LOW);
#endif

	return true;
}

void setup_wifi() {
	write_led(led_green,  HIGH);
	write_led(led_yellow, HIGH);

	draw_status(20);
	enable_wifi_debug();

	draw_status(21);
	WiFi.onEvent(WiFiEvent);

	connect_status_t cs = CS_IDLE;
	draw_status(22);
	start_wifi({ });

	Serial.print(F("Scanning for accesspoints"));
	draw_status(23);
	scan_access_points_start();

	draw_status(24);
	while(scan_access_points_wait() == false) {
#ifdef LED_BUILTIN
		digitalWrite(LED_BUILTIN, HIGH);
#endif
		Serial.print(F("."));
#ifdef LED_BUILTIN
		digitalWrite(LED_BUILTIN, LOW);
#endif
		delay(100);
	}

	draw_status(25);
	auto available_access_points = scan_access_points_get();
	Serial.printf("Found %zu accesspoints\r\n", available_access_points.size());

	draw_status(26);
	auto state = try_connect_init(wifi_targets, available_access_points, 300, progress_indicator);

	Serial.println(F("Connecting"));
	draw_status(27);
	for(;;) {
		cs = try_connect_tick(state);

		if (cs != CS_IDLE)
			break;

		Serial.print(F("."));
		delay(100);
	}

	write_led(led_green,  LOW);
	write_led(led_yellow, LOW);

	// could not connect
	if (cs == CS_CONNECTED)
		draw_status(29);
	else
		draw_status(28);
}

void loopw(void *) {
	Serial.println(F("Thread started"));

	int  cu_count = 0;
	unsigned long last_diskfree_update = 0;
	for(;;) {
		auto now = millis();
		if (now - draw_status_ts > 5000) {
// TODO powerdown display
		}

		if (now - last_diskfree_update >= update_df_interval * 1000 && update_df_interval != 0 && bs->is_idle()) {
			percentage_diskspace = bs->get_free_space_percentage();
			last_diskfree_update = now;
		}

		ntp.update();
		ArduinoOTA.handle();
		hundredsofasecondcounter = now / 10;

		vTaskDelay(100 / portTICK_PERIOD_MS);

		if (++cu_count >= 10) {
			uint64_t core0_tick_temp = core0_idle;
			uint64_t core1_tick_temp = core1_idle;
			core0_idle = core1_idle = 0;

			max_idle_ticks = std::max(core0_tick_temp, max_idle_ticks);
			max_idle_ticks = std::max(core1_tick_temp, max_idle_ticks);

			cpu_usage = ((100 - core0_tick_temp * 100 / max_idle_ticks) + (100 - core1_tick_temp * 100 / max_idle_ticks)) / 2;

			ram_free_kb = get_free_heap_space() / 1024;  // in kB

			ios.io_wait = ios.io_wait_cur / 10000;  // uS to 1/100th
			ios.io_wait_cur = 0;

			cu_count = 0;
		}
	}
}

void ls(fs::FS &fs, const String & name)
{
	Serial.print(F("Directory: "));
	Serial.println(name);

	File dir = fs.open(name);
	if (!dir) {
		Serial.println(F("Can't open"));
		write_led(led_red, LOW);
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

	TM.begin(21, 22, 4);
	TM.setBrightness(1);

	draw_status(1);

	uint64_t mac = ESP.getEfuseMac();
	uint8_t *chipid = reinterpret_cast<uint8_t *>(&mac);
	snprintf(name, sizeof name, "iESP-%02x%02x%02x%02x", chipid[2], chipid[3], chipid[4], chipid[5]);

	draw_status(2);

	Serial.println(F("iESP, (C) 2023-2024 by Folkert van Heusden <mail@vanheusden.com>"));
	Serial.println(F("Compiled on " __DATE__ " " __TIME__));
	Serial.print(F("GIT hash: "));
	Serial.println(version_str);
	Serial.print(F("System name: "));
	Serial.println(name);

	draw_status(3);

#ifdef LED_BUILTIN
	pinMode(LED_BUILTIN, OUTPUT);
#endif
	if (led_green != -1)
		pinMode(led_green, OUTPUT);
	if (led_yellow != -1)
		pinMode(led_yellow, OUTPUT);
	if (led_red != -1)
		pinMode(led_red, OUTPUT);

	draw_status(4);

	if (!LittleFS.begin()) {
		Serial.println(F("LittleFS.begin() failed"));
		draw_status(5);
	}

	draw_status(6);
	
	if (load_configuration() == false) {
		Serial.println(F("Failed to load configuration, using defaults!"));
		ls(LittleFS, "/");
		draw_status(7);
		fail_flash();
	}

	set_hostname(name);
	WiFi.onEvent(WiFiEvent);
#if defined(WEMOS32_ETH)
	//begin(int MISO_GPIO, int MOSI_GPIO, int SCLK_GPIO, int CS_GPIO, int INT_GPIO, int SPI_CLOCK_MHZ, int SPI_HOST, bool use_mac_from_efuse=false)
	bool eth_ok = false;
	if (ETH.begin(19, 23, 18, 5, 4, 9, 1, false) == true) {  // ENC28J60
		eth_ok = true;
		Serial.println(F("ENC28J60 ok!"));
	}

	if (!eth_ok) {
		Serial.println(F("ENC28J60 failed"));
		fail_flash();
	}
#elif defined(WEMOS32)
//
#else
	ETH.begin();  // ESP32-WT-ETH01, w32-eth01
#endif
	setup_wifi();
	init_logger(name);

	draw_status(8);

	ntp.begin();

	draw_status(10);

	esp_register_freertos_idle_hook_for_cpu(idle_task_0, 0);
	esp_register_freertos_idle_hook_for_cpu(idle_task_1, 1);

#if defined(ESP32)
	esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
	Serial.printf("Original pthread stack size: %d\r\n", cfg.stack_size);
	cfg.stack_size = 8192;
	esp_pthread_set_cfg(&cfg);
#endif

	draw_status(11);
	init_snmp(&snmp_, &snmp_data_, &ios, &is, &percentage_diskspace, &cpu_usage, &ram_free_kb, &stop);

	draw_status(13);
	bs = new backend_sdcard(led_green, led_yellow);
	draw_status(14);
	if (bs->begin() == false) {
		errlog("Failed to load initialize storage backend!");
		draw_status(15);
		fail_flash();
	}

	draw_status(16);
	scsi_dev = new scsi(bs, trim_level, &ios);

	draw_status(20);
	auto reset_reason = esp_reset_reason();
	if (reset_reason != ESP_RST_POWERON)
		errlog("Reset reason: %s (%d), software version %s", reset_name(reset_reason), reset_reason, version_str);
	else
		errlog("System (re-)started, software version %s", version_str);

	draw_status(25);
	esp_wifi_set_ps(WIFI_PS_NONE);

	draw_status(28);
	if (MDNS.begin(name))
		MDNS.addService("iscsi", "tcp", 3260);
	else
		errlog("Failed starting mdns responder");

	draw_status(30);
	heap_caps_register_failed_alloc_callback(heap_caps_alloc_failed_hook);

	draw_status(32);
	write_led(led_green,  HIGH);
	write_led(led_yellow, HIGH);
	Serial.printf("Waiting %d seconds for Ethernet: ", eth_wait_seconds);
	auto start = millis();
	while(eth_connected == false && (millis() - start) / 1000 < eth_wait_seconds) {
		delay(200);
		Serial.print(".");
	}

	if (!eth_connected)
		Serial.println(F("Not listening on Ethernet"));
	write_led(led_green,  LOW);
	write_led(led_yellow, LOW);

	draw_status(33);

	enable_OTA();

	draw_status(35);

	if (setCpuFrequencyMhz(240))
		Serial.println(F("Clock frequency set"));
	else
		Serial.println(F("Clock frequency NOT set"));

	draw_status(50);

	xTaskCreate(loopw, /* Function to implement the task */
			"loop2", /* Name of the task */
			10000,  /* Stack size in words */
			nullptr,  /* Task input parameter */
			127,  /* Priority of the task */
			&task2  /* Task handle. */
		   );

	draw_status(100);
}

void loop()
{
	draw_status(201);
	{
		char buffer[16];
#if defined(WEMOS32)
		auto ip = WiFi.localIP();
		snprintf(buffer, sizeof buffer, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
#else
		auto ipe = ETH.localIP();
		auto ipw = WiFi.localIP();
		if (ipe == IPAddress(uint32_t(0)))  // not connected to Ethernet? then use WiFi IP-address
			snprintf(buffer, sizeof buffer, "%d.%d.%d.%d", ipw[0], ipw[1], ipw[2], ipw[3]);
		else
			snprintf(buffer, sizeof buffer, "%d.%d.%d.%d", ipe[0], ipe[1], ipe[2], ipe[3]);
#endif

		Serial.print(F("Will listen on (in a bit): "));
		Serial.println(buffer);

		draw_status(205);
		com_sockets c(buffer, 3260, &stop);
		if (c.begin() == false) {
			errlog("Failed to initialize communication layer!");
			draw_status(210);
			fail_flash();
		}

		draw_status(220);
		server s(scsi_dev, &c, &is);
		Serial.printf("Free heap space: %u\r\n", get_free_heap_space());
		Serial.println(F("Go!"));
		draw_status(500);
		s.handler();
		draw_status(900);
	}

	if (ota_update) {
		draw_status(950);
		errlog("Halting for OTA update");

		for(;;)
			delay(1000);
	}
}
