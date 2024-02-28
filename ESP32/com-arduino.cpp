#include <Arduino.h>
#include <atomic>
#include <errno.h>
#include <cstring>
#include <unistd.h>
#if !defined(TEENSY4_1)
#include <WiFi.h>
#include <hardware/watchdog.h>
#endif

#include "com-arduino.h"
#include "log.h"
#include "utils.h"
#if defined(TEENSY4_1)
#include "snmp/snmp.h"  // ugly hack
extern snmp *snmp_;
#else
#include "wifi.h"
#endif


com_arduino::com_arduino(const int port): com(nullptr), port(port)
{
}

bool com_arduino::begin()
{
#if defined(TEENSY4_1)
	server = new qn::EthernetServer(port);
#else
	Serial.println(F("Set hostname"));
	WiFi.hostname("iPICO");

	Serial.println(F("Start WiFi"));
	WiFi.mode(WIFI_STA);
	Serial.println(F("Disable WiFi low power"));
	WiFi.noLowPowerMode();
	Serial.print(F("Connecting to: "));
	Serial.println(ssid);
	WiFi.begin(ssid, wifi_pw);
 
	Serial.println(F("Wait for connection:"));
	for(;;) {
		auto rc = WiFi.status();
		if (rc == WL_CONNECTED)
			break;

		delay(500);
		Serial.print(' ');
		Serial.print(rc);
	}
	Serial.println(F(""));

	auto rssi = WiFi.RSSI();
	Serial.print(F("Signal strength (RSSI): "));
	Serial.print(rssi);
	Serial.println(F(" dBm"));

	Serial.print(F("Will listen on (in a bit): "));
	Serial.println(WiFi.localIP());

	server = new WiFiServer(port);
#endif
	Serial.printf("Starting server on port %d\r\n", port);
	server->begin();

	return true;
}

com_arduino::~com_arduino()
{
#if !defined(TEENSY4_1)
	delete server;
#endif
}

std::string com_arduino::get_local_address()
{
#if defined(TEENSY4_1)
	auto ip = qn::Ethernet.localIP();
#else
	auto ip = WiFi.localIP();
#endif
	char buffer[16];
	snprintf(buffer, sizeof buffer, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

	return buffer;
}

com_client *com_arduino::accept()
{
#if defined(TEENSY4_1)
	Serial.println(F("Waiting for iSCSI connection..."));

	for(;;) {
		// ugly hack
		snmp_->poll();

		qn::Ethernet.maintain();

		auto wc = server->accept();  // is non-blocking
		if (wc) {
			Serial.println(F("New session!"));

			return new com_client_arduino(wc);
		}
	}
#else
	while(!server->hasClient()) {
		watchdog_update();
		delay(10);
	}

	auto wc = server->accept();

	return new com_client_arduino(wc);
#endif
}

#if defined(TEENSY4_1)
com_client_arduino::com_client_arduino(qn::EthernetClient & wc): wc(wc), com_client(nullptr)
{
}
#else
com_client_arduino::com_client_arduino(WiFiClient & wc): wc(wc), com_client(nullptr)
{
	wc.setNoDelay(true);
}
#endif

com_client_arduino::~com_client_arduino()
{
	wc.stop();
}

bool com_client_arduino::send(const uint8_t *const from, const size_t n)
{
	const uint8_t *p    = from;
	size_t         todo = n;

	while(todo > 0 && wc) {
#if !defined(TEENSY4_1)
		watchdog_update();
#endif

		ssize_t cur_n = wc.write(p, todo);
		if (cur_n < 0)
			break;

		if (cur_n > 0) {
			p    += cur_n;
			todo -= cur_n;
		}
	}

	if (wc)
		wc.flush();

	return todo == 0;
}

bool com_client_arduino::recv(uint8_t *const to, const size_t n)
{
	uint8_t *p    = to;
	size_t   todo = n;

	while(todo > 0 && wc) {
#if defined(TEENSY4_1)
		// ugly hack
		snmp_->poll();
#else
		watchdog_update();
#endif

		ssize_t cur_n = wc.read(p, todo);
		if (cur_n < 0)
			break;

		if (cur_n > 0) {
			p    += cur_n;
			todo -= cur_n;
		}
	}

	return todo == 0;
}

std::string com_client_arduino::get_endpoint_name() const
{
	auto ip = wc.remoteIP();
	char buffer[16];
	snprintf(buffer, sizeof buffer, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
	return buffer;
}
