// (C) 2023-2024 by Folkert van Heusden
// Released under MIT license

#include <csignal>
#include <cstdio>
#include <SPI.h>
#include <LittleFS.h>

#include "../server.h"

void setup()
{
	Serial.begin(115200);
}

void loop()
{
	server s;
	s.begin();

	s.handler();
}
