#include <cstdarg>
#include <cstdio>
#if defined(ARDUINO)
#include <NTP.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#else
#include <string>
#include <syslog.h>
#endif


#if defined(ARDUINO)
std::optional<std::string> syslog_host;
WiFiUDP UDP;
#endif
std::string name { "?" };

#if defined(ARDUINO)
extern NTP ntp;
#endif

thread_local char err_log_buf[192];

// TODO: into headerfile
extern void write_led(const int gpio, const int state);
extern int led_red;
extern bool is_network_up();

void errlog(const char *const fmt, ...)
{
	int offset = snprintf(err_log_buf, sizeof err_log_buf, "%s] ", name.c_str());

	va_list ap;
	va_start(ap, fmt);
	(void)vsnprintf(&err_log_buf[offset], sizeof(err_log_buf) - offset, fmt, ap);
	va_end(ap);

#if !defined(ARDUINO)
	syslog(LOG_ERR, "%s", err_log_buf);
	printf("%s\n", err_log_buf);
#else
	write_led(led_red, HIGH);

	Serial.printf("%04d-%02d-%02d %02d:%02d:%02d ", ntp.year(), ntp.month(), ntp.day(), ntp.hours(), ntp.minutes(), ntp.seconds());
	Serial.println(err_log_buf);

	if (syslog_host.has_value() && is_network_up()) {
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
	write_led(led_red, LOW);
#endif
}

void init_logger(const std::string & new_name)
{
	name = new_name;

#if defined(ESP32) || defined(RP2040)
	if (UDP.begin(514) == 0)
		Serial.println(F("UDP.begin(514) failed"));
#endif
}
