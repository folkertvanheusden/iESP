#include <atomic>
#include <errno.h>
#include <cstring>
#include <unistd.h>
#ifdef TEENSY4_1
#include <NativeEthernet.h>
#else
#include <WiFi.h>
#include <hardware/watchdog.h>
#endif

#include "com-arduino.h"
#include "log.h"
#include "utils.h"
#if defined(TEENSY4_1)
#include "snmp/snmp.h"  // ugly hack
#else
#include "wifi.h"
#endif


com_arduino::com_arduino(const int port): com(nullptr), port(port)
{
}

bool com_arduino::begin()
{
#if defined(TEENSY4_1)
	server = new EthernetServer(port);
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
	delete server;
}

std::string com_arduino::get_local_address()
{
#if defined(TEENSY4_1)
	auto ip = Ethernet.localIP();
#else
	auto ip = WiFi.localIP();
#endif
	char buffer[16];
	snprintf(buffer, sizeof buffer, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

	return buffer;
}

com_client *com_arduino::accept()
{
#if !defined(TEENSY4_1)
	while(!server->hasClient()) {
		watchdog_update();
		delay(10);
	}
#endif
	auto wc = server->accept();

	Serial.println(F("New session!"));

	return new com_client_arduino(wc);
}

#if defined(TEENSY4_1)
com_client_arduino::com_client_arduino(EthernetClient & wc): wc(wc), com_client(nullptr)
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

	while(todo > 0) {
		if (wc.connected() == false)
			return false;

#if !defined(TEENSY4_1)
		watchdog_update();
#endif

		size_t cur_n = wc.write(p, n);
		p    += cur_n;
		todo -= cur_n;

		if (todo)
			yield();
	}

	return true;
}

bool com_client_arduino::recv(uint8_t *const to, const size_t n)
{
	uint8_t *p    = to;
	size_t   todo = n;

	while(todo > 0) {
		size_t cur_n = wc.read(p, todo);
		p    += cur_n;
		todo -= cur_n;

#if defined(TEENSY4_1)
		// ugly hack
		extern snmp *snmp_;
		snmp_->poll();
#else
		watchdog_update();
#endif

		if (todo) {
			yield();

			if (wc.connected() == false)
				return false;
		}
	}

	return true;
}

std::string com_client_arduino::get_endpoint_name() const
{
	auto ip = wc.remoteIP();
	char buffer[16];
	snprintf(buffer, sizeof buffer, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
	return buffer;
}
