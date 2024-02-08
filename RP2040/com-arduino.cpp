#include <atomic>
#include <errno.h>
#include <cstring>
#include <unistd.h>
#include <WiFi.h>

#include "com-arduino.h"
#include "log.h"
#include "utils.h"
#include "wifi.h"


com_arduino::com_arduino(const int port): com(nullptr), port(port)
{
}

bool com_arduino::begin()
{
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

	Serial.printf("Starting server on port %d\r\n", port);
	server = new WiFiServer(port);
	server->begin();

	return true;
}

com_arduino::~com_arduino()
{
	delete server;
}

std::string com_arduino::get_local_address()
{
	auto ip = WiFi.localIP();
	char buffer[16];
	snprintf(buffer, sizeof buffer, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

	return buffer;
}

com_client *com_arduino::accept()
{
	while(!server->hasClient())
		delay(10);

	auto wc = server->accept();

	Serial.println(F("New session!"));

	return new com_client_arduino(wc);
}

com_client_arduino::com_client_arduino(WiFiClient & wc): wc(wc), com_client(nullptr)
{
	wc.setNoDelay(true);
}

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
	return wc.remoteIP().toString().c_str();
}
