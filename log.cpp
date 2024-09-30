#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <time.h>
#if defined(ARDUINO)
#include <NTP.h>
#if defined(TEENSY4_1)
#include <QNEthernet.h>
namespace qn = qindesign::network;
#else
#include <WiFi.h>
#include <WiFiUdp.h>
#endif
#else
#include <syslog.h>
#endif

#include "log.h"
#include "utils.h"


#if defined(ARDUINO)
std::optional<std::string> syslog_host;
#if defined(TEENSY4_1)
qn::EthernetUDP UDP;
#else
WiFiUDP UDP;
#endif
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

#if defined(ARDUINO)
void initlogger()
{
#if defined(ESP32) || defined(RP2040)
	if (UDP.begin(514) == 0)
		Serial.println(F("UDP.begin(514) failed"));
#endif
}

namespace logging {
	void sendsyslog(const logging::log_level_t ll, const char *const component, const std::string context, const char *fmt, ...)
	{
		thread_local char err_log_buf[192];

		int sl_nr = 3 /* "system daemons" */ * 8;  // see https://www.ietf.org/rfc/rfc3164.txt

		if (ll == ll_info)
			sl_nr += 6;
		else if (ll == ll_warning)
			sl_nr += 4;
		else if (ll == ll_error)
			sl_nr += 3;
		else
			sl_nr += 2;  // critical

		int offset = snprintf(err_log_buf, sizeof err_log_buf, "<%d>%s|%s] ", sl_nr, component, context.c_str());

		va_list ap;
		va_start(ap, fmt);
		(void)vsnprintf(&err_log_buf[offset], sizeof(err_log_buf) - offset, fmt, ap);
		va_end(ap);

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
	}
}
#else
namespace logging {
	static const char *logfile          = strdup("/tmp/iesp.log");
	log_level_t        log_level_file   = ll_debug;
	log_level_t        log_level_screen = ll_debug;

	void initlogger()
	{
	}

	void setlog(const char *const lf, const logging::log_level_t ll_file, const logging::log_level_t ll_screen)
	{
		free(const_cast<char *>(logfile));
		logfile = strdup(lf);

		log_level_file   = ll_file;
		log_level_screen = ll_screen;
	}

	void dolog(const logging::log_level_t ll, const char *const component, const std::string context, const char *fmt, ...)
	{
		if (ll < log_level_file && ll < log_level_screen)
			return;

		FILE *lfh = fopen(logfile, "a+");
		if (!lfh) {
			fprintf(stderr, "Cannot access log-file \"%s\": %s\n", logfile, strerror(errno));
			exit(1);
		}

		uint64_t now   = get_micros();
		time_t   t_now = now / 1000000;

		tm tm { };
		if (!localtime_r(&t_now, &tm))
			fprintf(stderr, "localtime_r: %s\n", strerror(errno));

		char *ts_str = nullptr;

		const char *const ll_names[] = { "debug  ", "info   ", "warning", "error  " };

		asprintf(&ts_str, "%04d-%02d-%02d %02d:%02d:%02d.%06d %s %s %s ",
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, int(now % 1000000),
				ll_names[ll], component, context.c_str());

		char *str = nullptr;

		va_list ap;
		va_start(ap, fmt);
		(void)vasprintf(&str, fmt, ap);
		va_end(ap);

		if (ll >= log_level_file)
			fprintf(lfh, "%s%s\n", ts_str, str);

		if (ll >= log_level_screen)
			printf("%s%s\n", ts_str, str);

		free(str);
		free(ts_str);

		fclose(lfh);
	}
}
#endif
