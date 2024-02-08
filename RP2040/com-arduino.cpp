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
	WiFi.hostname("iPICO");

	WiFi.mode(WIFI_STA);
	WiFi.begin(ssid, wifi_pw);
 
	while (WiFi.status() != WL_CONNECTED){
		delay(500);
		Serial.print(".");
	}
	Serial.println(F(""));

	auto rssi = WiFi.RSSI();
	Serial.print(F("Signal strength (RSSI): "));
	Serial.print(rssi);
	Serial.println(F(" dBm"));

	Serial.print(F("Will listen on (in a bit): "));
	Serial.println(WiFi.localIP());

	server = new WiFiServer(port);

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

	return new com_client_arduino(wc);
}

com_client_arduino::com_client_arduino(WiFiClient & wc): wc(wc), com_client(nullptr)
{
}

com_client_arduino::~com_client_arduino()
{
	wc.stop();
}

bool com_client_arduino::send(const uint8_t *const from, const size_t n)
{
	if (wc.connected() == false)
		return false;

	wc.write(from, n);
	return true;
}

bool com_client_arduino::recv(uint8_t *const to, const size_t n)
{
	if (wc.connected() == false)
		return false;

	wc.readBytes(to, n);
	return true;
}

std::string com_client_arduino::get_endpoint_name() const
{
	return wc.remoteIP().toString().c_str();
}
