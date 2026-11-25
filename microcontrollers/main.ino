// (C) 2023-2026 by Folkert van Heusden
// Released under MIT license

#include <atomic>
#include <ArduinoJson.h>
#if !defined(TEENSY4_1)
#include <ArduinoOTA.h>
#endif
#include <csignal>
#include <cstdio>
#if !defined(TEENSY4_1)
#include <esp_debug_helpers.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <ESPmDNS.h>
#endif
#if defined(WEMOS32)
//
#elif defined(WAVESHARE_ESP32_ETH)
#include <SPI.h>
#include <ETH.h>
#elif defined(WEMOS32_ETH)
#include <ESP32-ENC28J60.h>
#elif !defined(TEENSY4_1)
#include <ETH.h>
#if defined(WT_ETH01)
#define ETH_ADDR        1
#define ETH_POWER_PIN   16//-1 //16 // Do not use it, it can cause conflict during the software reset.
#define ETH_POWER_PIN_ALTERNATIVE 16 //17
#define ETH_MDC_PIN    23
#define ETH_MDIO_PIN   18
#define ETH_TYPE       ETH_PHY_LAN8720
#define ETH_CLK_MODE    ETH_CLOCK_GPIO17_OUT // ETH_CLOCK_GPIO0_IN
#endif
#endif
#if defined(ESP32)
#include <esp_pthread.h>
#endif
#include <LittleFS.h>
#include <NTP.h>
#if !defined(WT_ETH01)
#include <TM1637.h>
#endif

#include "backend-cache.h"
#if defined(TEENSY4_1)
#include "backend-sdcard-teensy41.h"
#include "com-arduino.h"
#else
#include "backend-sdcard.h"
#include "com-sockets.h"
#endif
#include "log.h"
#include "server.h"
#include "snmp.h"
#include "utils.h"
#if defined(TEENSY4_1)
#include <QNEthernet.h>
namespace qn = qindesign::network;
#else
#include "wifi-management.h"
#include "wifi.h"
#endif
#include "version.h"
#include "snmp/snmp.h"


bool      ota_update { false   };
char      name[24]   { 0       };
backend  *bs         { nullptr };
scsi     *scsi_dev   { nullptr };
backend_stats_t bstats { };
std::atomic_bool stop { false  };
std::string disk_name { "test.dat" };
server   *s          { nullptr };

#if defined(WT_ETH01)
int led_green  = -1;
int led_yellow = -1;
int led_red    = -1;
#elif defined(WEMOS32_ETH) || defined(WEMOS32)
int led_green  = -1;
int led_yellow = -1;
int led_red    = -1;
#elif defined(TEENSY4_1)
int led_green  = 7;
int led_yellow = 6;
int led_red    = 5;
#elif defined(OLIMEX)
int led_green  = 33;
int led_yellow = 16;
int led_red    = 32;
#else
int led_green  = -1;
int led_yellow = -1;
int led_red    = -1;
#endif
#if defined(WT_ETH01)
int pin_SD_MISO= -1;
int pin_SD_MOSI= -1;
int pin_SD_SCLK= -1;
int pin_SD_CS  = -1;
std::optional<int> spi_speed { 14 };
#elif defined(WAVESHARE_ESP32_ETH)
SPIClass hspi(HSPI);
int pin_SD_MISO= 5;
int pin_SD_MOSI= 6;
int pin_SD_SCLK= 7;
int pin_SD_CS  = 4;
std::optional<int> spi_speed { 14 };
#else
int pin_SD_MISO= 19;
int pin_SD_MOSI= 23;
int pin_SD_SCLK= 18;
int pin_SD_CS  =  5;
std::optional<int> spi_speed;
#endif

snmp      *snmp_      { nullptr };
snmp_data *snmp_data_ { nullptr };

DynamicJsonDocument cfg(4096);
#define IESP_CFG_FILE "/cfg-iESP.json"

std::vector<std::pair<std::string, std::string> > wifi_targets;
int trim_level = 1;

#if defined(TEENSY4_1)
LittleFS_Program myfs;

qn::EthernetUDP snmp_udp;
qn::EthernetUDP ntp_udp;
#else
TaskHandle_t task2;

WiFiUDP snmp_udp;
WiFiUDP ntp_udp;
#endif

NTP ntp(ntp_udp);

uint32_t hundredsofasecondcounter = 0;
iscsi_stats_t is { };
int cpu_usage            = 0;
int ram_free_kb          = 0;
int eth_wait_seconds     = 10;
int update_df_interval   = 0;
int percentage_diskspace = 0;

#if !defined(WT_ETH01)
TM1637 *TM { nullptr };
#endif

long int draw_status_ts = 0;

void draw_status(const uint32_t v) {
#if defined(TEENSY4_1)
  CrashReport.breadcrumb(5, v);
#endif

#if defined(WT_ETH01) || defined(WAVESHARE_ESP32_ETH)
  Serial.printf(" SYSTEM STATE: 0x%04x\r\n", v);
  Serial.flush();
#else
	if (TM) {
		draw_status_ts = millis();

		TM->setBrightness(8);
		TM->displayInt(v);
	}
#endif
}

int get_diskspace(void *const context)
{
	backend *const bf = reinterpret_cast<backend *>(context);

	return bf->get_free_space_percentage();
}

void write_led(const int gpio, const int state) {
#if 0
  const char *which = "?";
  if (gpio == led_green)
    which = "green";
  else if (gpio == led_yellow)
    which = "yellow";
  else if (gpio == led_red)
    which = "red";
  Serial.printf("%p set led on gpio %d (%s) to %d\r\n", __builtin_return_address(0), gpio, which, state);
#endif
	if (gpio != -1)
		digitalWrite(gpio, state);
}

void fail_flash() {
  Serial.flush();
	DOLOG(logging::ll_error, "fail_flash", "-", "System cannot continue");

	write_led(led_green,  LOW);
	write_led(led_yellow, LOW);

	for(;;) {
    bool state = get_millis() & 1024;
#ifdef LED_BUILTIN
		digitalWrite(LED_BUILTIN, state);
#endif
		write_led(led_red, state);
#if !defined(TEENSY4_1) && !defined(RP2040W)
		ArduinoOTA.handle();
#endif
		delay(1);
#ifdef LED_BUILTIN
		digitalWrite(LED_BUILTIN, state);
#endif
		write_led(led_red, !state);
		delay(1);
	}
}

bool load_configuration() {
#if defined(TEENSY4_1)
	File data_file = myfs.open(IESP_CFG_FILE, FILE_READ);
#else
	File data_file = LittleFS.open(IESP_CFG_FILE, "r");
#endif
	if (!data_file)
		return false;

	auto error = deserializeJson(cfg, data_file);
	if (error) {  // this should not happen
		data_file.close();
		return false;
	}

#if !defined(TEENSY4_1)
	JsonArray w_aps_ar = cfg["wifi"].as<JsonArray>();
	for(JsonObject p: w_aps_ar) {
		auto ssid = p["ssid"];
		auto psk  = p["psk"];

		Serial.print(F("Will search for: "));
		Serial.println(ssid.as<String>());

		wifi_targets.push_back({ ssid, psk });
	}
#endif

	syslog_host = cfg["syslog-host"].as<const char *>();
	if (syslog_host.value().empty())
		syslog_host.reset();
	else
		Serial.printf("Syslog host: %s\r\n", syslog_host.value().c_str());

	if (cfg.containsKey("syslog-is-broadcast"))
		syslog_is_broadcast = cfg["syslog-is-broadcast"].as<bool>();

	if (cfg.containsKey("trim-level"))
		trim_level = cfg["trim-level"].as<int>();

	if (cfg.containsKey("eth-wait-time"))
		eth_wait_seconds = cfg["eth-wait-time"].as<int>();

	if (cfg.containsKey("update-df-interval"))
		update_df_interval = cfg["update-df-interval"].as<int>();

	if (cfg.containsKey("led-green"))
		led_green = cfg["led-green"].as<int>();

	if (cfg.containsKey("led-yellow"))
		led_yellow = cfg["led-yellow"].as<int>();

	if (cfg.containsKey("led-red"))
		led_red = cfg["led-red"].as<int>();

	if (cfg.containsKey("SD-MISO"))
		pin_SD_MISO = cfg["SD-MISO"].as<int>();

	if (cfg.containsKey("SD-MOSI"))
		pin_SD_MOSI = cfg["SD-MOSI"].as<int>();

	if (cfg.containsKey("SD-SCLK"))
		pin_SD_SCLK = cfg["SD-SCLK"].as<int>();

	if (cfg.containsKey("SD-CS"))
		pin_SD_CS = cfg["SD-CS"].as<int>();

	if (cfg.containsKey("log-level"))
		logging::log_level_syslog = logging::parse_ll(cfg["log-level"].as<std::string>());

	if (cfg.containsKey("spi-speed"))
		spi_speed = cfg["spi-speed"].as<int>();

	if (cfg.containsKey("disk-name"))
		disk_name = cfg["disk-name"].as<int>();

	data_file.close();

#if !defined(TEENSY4_1) && !defined(OLIMEX) && !defined(WAVESHARE_ESP32_ETH) && !defined(WT_ETH01) && !defined(WEMOS32_ETH)
	auto n = wifi_targets.size();
	Serial.printf("Loaded configuration parameters for %zu WiFi access points\r\n", n);
	if (n == 0) {
		Serial.println(F("Cannot continue without WiFi access"));
		fail_flash();
	}
#endif

	return true;
}

#if !defined(TEENSY4_1)
void enable_OTA() {
	ArduinoOTA.setPort(3232);
	ArduinoOTA.setHostname(name);
	ArduinoOTA.setPassword("iojasdsjiasd");

	ArduinoOTA.onStart([]() {
			DOLOG(logging::ll_info, "enable_OTA", "-", "OTA start\n");
			ota_update = true;
			stop = true;
			LittleFS.end();
			});
	ArduinoOTA.onEnd([]() {
			DOLOG(logging::ll_info, "enable_OTA", "-", "OTA end");
			});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
			Serial.printf("OTA progress: %u%%\r", progress * 100 / total);
			});
	ArduinoOTA.onError([](ota_error_t error) {
			write_led(led_red, HIGH);
			DOLOG(logging::ll_info, "enable_OTA", "-", "OTA error[%u]: ", error);
			if (error == OTA_AUTH_ERROR) DOLOG(logging::ll_info, "enable_OTA", "-", "auth failed");
			else if (error == OTA_BEGIN_ERROR) DOLOG(logging::ll_info, "enable_OTA", "-", "begin failed");
			else if (error == OTA_CONNECT_ERROR) DOLOG(logging::ll_info, "enable_OTA", "-", "connect failed");
			else if (error == OTA_RECEIVE_ERROR) DOLOG(logging::ll_info, "enable_OTA", "-", "receive failed");
			else if (error == OTA_END_ERROR) DOLOG(logging::ll_info, "enable_OTA", "-", "end failed");
			});
	ArduinoOTA.begin();

	Serial.println(F("OTA ready"));
}
#endif

volatile bool eth_connected  = false;
volatile bool wifi_connected = false;

bool is_network_up()
{
	return eth_connected || wifi_connected;
}

#if !defined(TEENSY4_1)
void heap_caps_alloc_failed_hook(size_t requested_size, uint32_t caps, const char *function_name)
{
	write_led(led_red, HIGH);
	DOLOG(logging::ll_error, "heap_caps_alloc_failed_hook", "-", "%s was called but failed to allocate %zu bytes with 0x%x capabilities (by %p)", function_name, requested_size, caps, __builtin_return_address(0));

  static bool first = true;
  if (first) {
    first = false;
    heap_caps_dump(MALLOC_CAP_8BIT);
  }

	esp_backtrace_print(25);
	write_led(led_red, LOW);
}

uint32_t idle_cnt = 0;
void idle_task(void *)
{
  for(;;) {
    int64_t start = esp_timer_get_time();
    vTaskDelay(0);
    idle_cnt += esp_timer_get_time() - start;
  }
}

void loopw(void *)
{
	Serial.println(F("Thread started"));

	int cu_count = 0;

	for(;;) {
    yield();

		auto now = millis();
		if (now - draw_status_ts > 5000) {
#if !defined(WT_ETH01)
			if (TM)
				TM->setBrightness(1);
#endif
			draw_status_ts = now;
		}

#ifdef LED_BUILTIN
    if (s)
      digitalWrite(LED_BUILTIN, ((cu_count / 10) & 1) && s->is_active());
#endif

		ntp.update();
		ArduinoOTA.handle();
		hundredsofasecondcounter = now / 10;

		vTaskDelay(25 / portTICK_PERIOD_MS);

		if (++cu_count >= 40) {
      uint32_t cpu_usage_temp = idle_cnt;
      idle_cnt = 0;
      cpu_usage = 100 - (99.9 / 90. * cpu_usage_temp / 1000) / 10;

			ram_free_kb = get_free_heap_space() / 1024;  // in kB

      bs->get_and_reset_stats(&bstats);
      bstats.io_wait_ticks = bstats.io_wait * 10000;

			cu_count = 0;
		}
	}
}
#endif

#if !defined(TEENSY4_1)
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
#endif

void setup() {
	Serial.begin(115200);
  auto end = millis() + 2500;
#if defined(TEENSY4_1)
	while(!Serial && end > millis()) {
  }
#else
	while(!Serial && end > millis())
		yield();
  Serial.setDebugOutput(bool(Serial));
#endif

#if defined(TEENSY4_1)
	if (CrashReport) {
		Serial.println(CrashReport);
		CrashReport.clear();
	}
#endif

#if !defined(TEENSY4_1) && !defined(WT_ETH01) && !defined(OLIMEX) && !defined(WAVESHARE_ESP32_ETH)
	TM = new TM1637();
	TM->begin(21, 22, 4);  // TEENSY4.1: pins
	TM->setBrightness(1);
#endif

	draw_status(1);

#if defined(TEENSY4_1)
	uint8_t mac[6] { 0 };
	teensyMAC(mac);
	snprintf(name, sizeof name, "iTEENSY-%02x%02x%02x%02x", mac[2], mac[3], mac[4], mac[5]);
#else
	uint64_t mac = ESP.getEfuseMac();
	uint8_t *chipid = reinterpret_cast<uint8_t *>(&mac);
	snprintf(name, sizeof name, "iESP-%02x%02x%02x%02x", chipid[2], chipid[3], chipid[4], chipid[5]);
#endif

	draw_status(2);

	Serial.println(F("iESP, (C) 2023-2026 by Folkert van Heusden <folkert@vanheusden.com>"));
	Serial.println(F("Compiled on " __DATE__ " " __TIME__));
	Serial.print(F("GIT hash: "));
	Serial.println(version_str);
	Serial.print(F("System name: "));
	Serial.println(name);

#if !defined(TEENSY4_1)
	auto reset_reason = esp_reset_reason();
	if (reset_reason != ESP_RST_POWERON)
		DOLOG(logging::ll_error, "setup", "-", "Reset reason: %s (%d), software version %s, name: %s", reset_name(reset_reason), reset_reason, version_str, name);
	else
		DOLOG(logging::ll_info, "setup", "-", "System (re-)started, software version %s, name: %s", version_str, name);
#else
		DOLOG(logging::ll_info, "setup", "-", "System started, software version %s, name: %s", version_str, name);
#endif

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
#if defined(TEENSY4_1)
	if (!myfs.begin(4096))
#else
	if (!LittleFS.begin())
#endif
	{
		Serial.println(F("LittleFS.begin() failed"));
	}

	draw_status(5);
  Serial.println(psramInit());

	draw_status(6);

	if (load_configuration() == false) {
		Serial.println(F("Failed to load configuration, using defaults!"));
#if 0
#if defined(TEENSY4_1)
		do_ls(myfs, "/");
#else
		do_ls(LittleFS, "/");
#endif
#endif
		draw_status(7);
//		fail_flash();
	}

	draw_status(8);

#if defined(TEENSY4_1)
	qn::Ethernet.setHostname(name);
#else
  if (wifi_targets.empty() == false)
    wifi_connected = setup_wifi();
  else
    WiFi.onEvent(WiFiEventHandler);
	set_hostname(name);
#endif
#if defined(WEMOS32_ETH)
	//begin(int MISO_GPIO, int MOSI_GPIO, int SCLK_GPIO, int CS_GPIO, int INT_GPIO, int SPI_CLOCK_MHZ, int SPI_HOST, bool use_mac_from_efuse=false)
#define ENC_SPI_HOST   3
#define SPI_CLOCK_MHZ  8
#define INT_GPIO       4
#define MISO_GPIO 19
#define MOSI_GPIO 23
#define SCLK_GPIO 18
#define CS_GPIO   5
	bool eth_ok = false;
	if (ETH.begin(MISO_GPIO, MOSI_GPIO, SCLK_GPIO, CS_GPIO, INT_GPIO, SPI_CLOCK_MHZ, ENC_SPI_HOST) == true) {  // ENC28J60
		eth_ok = true;
		Serial.println(F("ENC28J60 ok!"));
	}

	if (!eth_ok) {
		Serial.println(F("ENC28J60 failed"));
		fail_flash();
	}

#elif defined(WAVESHARE_ESP32_ETH)
#ifdef ETH_POWER_PIN
  pinMode(ETH_POWER_PIN, OUTPUT);
  digitalWrite(ETH_POWER_PIN, HIGH);
#endif

#define ETH_PHY_TYPE ETH_PHY_W5500
#define ETH_PHY_ADDR 1
#define ETH_PHY_CS   14
#define ETH_PHY_IRQ  10
#define ETH_PHY_RST  9
#define ETH_SPI_SCK  13
#define ETH_SPI_MISO 12
#define ETH_SPI_MOSI 11
  hspi.begin(ETH_SPI_SCK, ETH_SPI_MISO, ETH_SPI_MOSI, ETH_PHY_CS);
  if (ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_CS, ETH_PHY_IRQ, ETH_PHY_RST, hspi) == false)
    Serial.print(F("Failed to initialize Ethernet"));

#elif defined(TEENSY4_1)
	if (qn::Ethernet.begin(mac) == 0) {
		Serial.println(F("Failed to configure Ethernet using DHCP"));

		if (qn::Ethernet.hardwareStatus() == qindesign::network::EthernetNoHardware)
			Serial.println(F("Ethernet shield was not found"));
		else if (qn::Ethernet.linkStatus() == qindesign::network::LinkOFF)
			Serial.println(F("Ethernet cable is not connected"));
	}
	else {
		Serial.print(F("Ethernet initialized ("));
		Serial.print(qn::Ethernet.localIP());
		Serial.println(F(")"));
	}
#elif defined(OLIMEX)
  if (ETH.begin(ETH_PHY_LAN8720, 0, 23, 18, 12, ETH_CLOCK_GPIO17_OUT) == false)
		Serial.println(F("Failed to init ETH"));
#elif defined(WEMOS32)
//
#else
  pinMode(ETH_POWER_PIN_ALTERNATIVE, OUTPUT);
  digitalWrite(ETH_POWER_PIN_ALTERNATIVE, HIGH);

  if (ETH.begin() == false)
    Serial.println(F("Ethernet init FAILED"));
#endif
	initlogger();

	draw_status(9);
	ntp.begin();

#if defined(ESP32)
	draw_status(10);
	esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
	Serial.printf("Original pthread stack size: %d\r\n", cfg.stack_size);
	cfg.stack_size = 8192;
	esp_pthread_set_cfg(&cfg);
#endif

	draw_status(11);
  backend *btemp { };
#if defined(TEENSY4_1)
	btemp = new backend_sdcard_teensy41(led_green, led_yellow, disk_name);
#elif defined(OLIMEX)
	btemp = new backend_sdcard(led_green, led_yellow, -1, -1, -1, -1, spi_speed, disk_name);
#else
  Serial.printf("LEDgreen: %d, LEDyellow: %d, MISO: %d, MOSI: %d, SCLK: %d, CS: %d\r\n", led_green, led_yellow, pin_SD_MISO, pin_SD_MOSI, pin_SD_SCLK, pin_SD_CS);
	btemp = new backend_sdcard(led_green, led_yellow, pin_SD_MISO, pin_SD_MOSI, pin_SD_SCLK, pin_SD_CS, spi_speed, disk_name);
#endif

#if defined(NOCACHE)
  size_t free_cache_store = 0;
#elif defined(ESP32)
  size_t free_cache_store = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#else
  size_t free_cache_store = get_free_heap_space();
#endif
  constexpr const size_t min_free = 512 * 1024;  // 0.5 MB
  // leave some room for std::list and std::unordered_map overhead
  free_cache_store = (free_cache_store * 2) / 3;
  if (free_cache_store > min_free + btemp->get_block_size() * 2) {
    size_t cache_size = free_cache_store - min_free;
    bs = new backend_cache(btemp, cache_size);
    Serial.printf("Using disk-cache of %u kB\r\n", int(cache_size / 1024));
  }
  else {
    bs = btemp;
  }

#if !defined(TEENSY4_1)
	draw_status(12);
  xTaskCreate(idle_task, "idle_task", 2048, nullptr, 0, nullptr);
#endif

	draw_status(13);
	init_snmp(&snmp_, &snmp_data_, &is, get_diskspace, bs, &bstats, &cpu_usage, &ram_free_kb, &stop, 161);

	draw_status(14);
	if (bs->begin() == false) {
		DOLOG(logging::ll_error, "setup", "-", "Failed to load initialize storage backend!");
		draw_status(15);
		fail_flash();
	}

	draw_status(16);
	scsi_dev = new scsi(bs, trim_level);

	draw_status(28);
#if defined(TEENSY4_1)
	if (qn::MDNS.begin(name))
		qn::MDNS.addService("iscsi", "tcp", 3260);
	else
		DOLOG(logging::ll_error, "setup", "-", "Failed starting MDNS responder");
#else
	if (MDNS.begin(name))
		MDNS.addService("iscsi", "tcp", 3260);
	else
		DOLOG(logging::ll_error, "setup", "-", "Failed starting MDNS responder");
#endif

#if !defined(TEENSY4_1)
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
#endif

#if !defined(TEENSY4_1)
	draw_status(33);
	enable_OTA();

	draw_status(35);
	if (setCpuFrequencyMhz(240))
		Serial.println(F("Clock frequency set"));
	else
		Serial.println(F("Clock frequency NOT set"));
  Serial.flush();

	draw_status(50);

	xTaskCreate(loopw, /* Function to implement the task */
			"loop2", /* Name of the task */
			16384,  /* Stack size in words */
			nullptr,  /* Task input parameter */
			configMAX_PRIORITIES - 2,  /* Priority of the task */
			&task2  /* Task handle. */
		   );
#endif

	draw_status(100);
}

// because threading is problematic on the Teensy4.1
void idle_poll()
{
#if defined(TEENSY4_1)
		snmp_->poll();

    ram_free_kb = get_free_heap_space();
#endif
}

void loop()
{
	draw_status(201);
	{
		char        buffer[16];
    const char *connection = "?";
#if defined(TEENSY4_1)
		auto ip = qn::Ethernet.localIP();
		snprintf(buffer, sizeof buffer, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    connection = "Ethernet";
#elif defined(WEMOS32)
		auto ip = WiFi.localIP();
		snprintf(buffer, sizeof buffer, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    connection = "WiFi";
#else
		auto ipe = ETH.localIP();
		auto ipw = WiFi.localIP();
		if (ipe == IPAddress(uint32_t(0))) {  // not connected to Ethernet? then use WiFi IP-address
			snprintf(buffer, sizeof buffer, "%d.%d.%d.%d", ipw[0], ipw[1], ipw[2], ipw[3]);
      connection = "WiFi";
    }
		else {
			snprintf(buffer, sizeof buffer, "%d.%d.%d.%d", ipe[0], ipe[1], ipe[2], ipe[3]);
      connection = "Ethernet";
    }
#endif
		Serial.printf("Will listen on (in a bit): %s (%s)\r\n", buffer, connection);

		draw_status(205);
#if defined(TEENSY4_1)
		com_arduino c(3260, idle_poll);
#else
		com_sockets c(buffer, 3260, &stop);
#endif
		if (c.begin() == false) {
			DOLOG(logging::ll_error, "loop", "-", "Failed to initialize communication layer!");
			draw_status(210);
			fail_flash();
		}

		draw_status(220);
#if defined(ISCSI_CHAP_AUTH_USER)
		s = new server(scsi_dev, &c, &is, "test", false, { ISCSI_CHAP_AUTH_USER, ISCSI_CHAP_AUTH_PASSWORD });
#else
		s = new server(scsi_dev, &c, &is, "test", false, { });
#endif
		Serial.printf("Free heap space: %u\r\n", get_free_heap_space());
		Serial.println(F("Go!"));
		draw_status(500);
		s->handler();
		draw_status(900);
	}

	if (ota_update) {
		draw_status(950);
		DOLOG(logging::ll_info, "loop", "-", "Halting for OTA update");

		for(;;)
			delay(1000);
	}
}
