#include <cstdarg>
#include <cstdio>
#ifdef linux
#include <syslog.h>
#else
#include <WiFi.h>
#include <WiFiUdp.h>
#endif


#ifndef linux
std::optional<std::string> syslog_host;
WiFiUDP UDP;
#endif

thread_local char err_log_buf[128];

void errlog(const char *const fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	(void)vsnprintf(err_log_buf, sizeof err_log_buf, fmt, ap);
	va_end(ap);

#ifdef linux
	syslog(LOG_ERR, "%s", err_log_buf);
	printf("%s\n", err_log_buf);
#else
	Serial.println(err_log_buf);

	if (syslog_host.has_value()) {
		IPAddress ip;
		static bool failed = false;
		if (!ip.fromString(syslog_host.value().c_str()) && failed == false) {
			failed = true;
			Serial.printf("Problem converting \"%s\" to an internal representation\r\n", syslog_host.value().c_str());
		}

		if (!failed) {
			UDP.beginPacket(ip, 514);
			UDP.printf(err_log_buf);
			UDP.endPacket();
		}
	}
#endif
}

void init_logger()
{
#ifndef linux
	if (UDP.begin(514) == 0)
		Serial.println(F("UDP.begin(514) failed"));
#endif
}
