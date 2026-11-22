// (C) 2024 by Folkert van Heusden
// Released under MIT license

#include <atomic>
#include <csignal>
#include <cstdio>
#include <SPI.h>
#include <WiFi.h>
#include <hardware/watchdog.h>

// Build options (set in platformio.ini build_flags):
//   -DIESP_BACKEND_VIRTUAL_FLASH   serve a read-only LUN compiled into the firmware (key-data.h)
//                                  instead of an image file on the SD card
//   -DIESP_TARGET_NAME=\"name\"      iSCSI target name (default "test")
#if defined(IESP_BACKEND_VIRTUAL_FLASH)
#include "backend-virtual-flash.h"
#else
#include "backend-sdcard-rp2040w.h"
#endif
#include "com-arduino.h"
#include "random.h"
#include "server.h"
#include "version.h"
#include "wifi.h"

#if !defined(IESP_TARGET_NAME)
#define IESP_TARGET_NAME "test"
#endif


std::atomic_bool        stop { false   };
com                    *c    { nullptr };
backend                *bs   { nullptr };
scsi                   *sd   { nullptr };
server                 *s    { nullptr };
iscsi_stats_t           is;
volatile bool           wifi_connected { false };
int                     led_green  {  17 };
int                     led_yellow {  18 };
int                     led_red    { -1  };

bool is_network_up()
{
	return wifi_connected;
}

void write_led(const int gpio, const int state) {
	if (gpio != -1)
		digitalWrite(gpio, state);
}

void setup()
{
	Serial.begin(115200);
	// Wait briefly for a USB serial console, but don't block when running headless.
	const unsigned long serial_wait_start = millis();
	while(!Serial && millis() - serial_wait_start < 3000)
		delay(10);
	Serial.setDebugOutput(bool(Serial));

	Serial.println(F("iESP (for RP2040W), (C) 2023-2024 by Folkert van Heusden <mail@vanheusden.com>"));
	Serial.println(F("Compiled on " __DATE__ " " __TIME__));
	Serial.print(F("GIT hash: "));
	Serial.println(version_str);

	Serial.print(F("Free memory at start: "));
	Serial.println(rp2040.getFreeHeap());

	if (rp2040.isPicoW() == false)
		Serial.println(F("This is NOT a Pico-W, this program will fail!"));

	if (watchdog_caused_reboot())
		Serial.println(F("Rebooted by watchdog"));

	if (led_green != -1)
		pinMode(led_green, OUTPUT);
	if (led_yellow != -1)
		pinMode(led_yellow, OUTPUT);
	if (led_red != -1)
		pinMode(led_red, OUTPUT);

	try {
		c = new com_arduino(3260, idle_poll);
		wifi_connected = c->begin();
		if (wifi_connected == false) {
			Serial.println(F("Failed to initialize com-layer: rebooting to retry"));
			delay(1000);
			rp2040.reboot();
		}

		init_my_getrandom();

#if defined(IESP_BACKEND_VIRTUAL_FLASH)
		Serial.println(F("Init virtual flash (read-only)"));
		bs = new backend_virtual_flash(led_green);
#else
		Serial.println(F("Init SD card"));
		bs = new backend_sdcard_rp2040w(led_green, led_yellow, FILENAME);
#endif
		bs->begin();

		Serial.println(F("Create SCSI instance"));
		sd = new scsi(bs, 1);

		Serial.print(F("Instantiate iSCSI server, target name: "));
		Serial.println(F(IESP_TARGET_NAME));
		s = new server(sd, c, &is, IESP_TARGET_NAME, false);

		Serial.print(F("Free memory after full init: "));
		Serial.println(rp2040.getFreeHeap());

		Serial.print(F("Enable watchdog"));
		watchdog_enable(2500, 1);  // 2.5s

		Serial.println(F("Setup step finished"));
	}
	catch(...) {
		Serial.println(F("An exception occured during init"));
	}
}

void idle_poll()
{
}

void loop()
{
	Serial.print(millis());
	Serial.println(F(" Go!"));

	try {
		s->handler();
	}
	catch(...) {
		Serial.println(F("An exception occured during run-time"));
	}
}
