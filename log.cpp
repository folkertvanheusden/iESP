#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <time.h>
#if defined(ARDUINO)
#if !defined(RP2040W)
#include <NTP.h>
#endif
#if defined(TEENSY4_1)
#include <QNEthernet.h>
namespace qn = qindesign::network;
#else
#include <WiFi.h>
#include <WiFiUdp.h>
#endif
#elif !defined(__MINGW32__)
#include <syslog.h>
#endif

#include "log.h"
#include "utils.h"


#if defined(ARDUINO)
std::optional<std::string> syslog_host;
#if defined(TEENSY4_1)
qn::EthernetUDP UDP_socket;
#else
WiFiUDP UDP_socket;
#endif
#endif
std::string name { "?" };

#if defined(ARDUINO) && !defined(RP2040W)
extern NTP ntp;
#endif

constexpr int err_log_buf_len = 256;
#if defined(RP2040W)
char err_log_buf[err_log_buf_len];
#else
thread_local char err_log_buf[err_log_buf_len];
#endif

// TODO: into headerfile
extern void write_led(const int gpio, const int state);
extern int led_red;
extern bool is_network_up();

namespace logging {
	log_level_t parse_ll(const std::string & str)
	{
		if (str == "debug")
			return logging::ll_debug;

		if (str == "info")
			return logging::ll_info;

		if (str == "warning")
			return logging::ll_warning;

		if (str == "error")
			return logging::ll_error;

#if !defined(ARDUINO)
		fprintf(stderr, "Log level \"%s\" not understood\n", str.c_str());
		exit(1);
#endif

		return logging::ll_debug;
	}
}

#if defined(ARDUINO)
void initlogger()
{
#if defined(ESP32)
	if (UDP_socket.begin(514) == 0)
		Serial.println(F("UDP_socket.begin(514) failed"));
#endif
}

namespace logging {
	log_level_t log_level_syslog = logging::ll_info;

	void sendsyslog(const logging::log_level_t ll, const char *const component, const std::string context, const char *fmt, ...)
	{
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
		if (offset == -1)
			offset = 0;  // snprintf failed, proceeed without component etc

		va_list ap;
		va_start(ap, fmt);
		(void)vsnprintf(&err_log_buf[offset], sizeof(err_log_buf) - offset, fmt, ap);
		va_end(ap);

		if (ll >= ll_warning)
			write_led(led_red, HIGH);

		if (syslog_host.has_value() && is_network_up()) {
			IPAddress ip;
			static bool failed = false;
			if (!ip.fromString(syslog_host.value().c_str()) && failed == false) {
				failed = true;
				Serial.printf("Problem converting \"%s\" to an internal representation\r\n", syslog_host.value().c_str());
			}

			if (!failed) {
				UDP_socket.beginPacket(ip, 514);
				UDP_socket.printf(err_log_buf);
				UDP_socket.endPacket();
			}
		}
		else {
#if !defined(RP2040W)
			Serial.printf("%04d-%02d-%02d %02d:%02d:%02d ", ntp.year(), ntp.month(), ntp.day(), ntp.hours(), ntp.minutes(), ntp.seconds());
#endif
			Serial.println(err_log_buf);
		}

		if (ll >= ll_warning)
			write_led(led_red, LOW);
	}
}
#else
namespace logging {
	static const char *logfile          = strdup("/tmp/iesp.log");
	log_level_t        log_level_file   = logging::ll_debug;
	log_level_t        log_level_screen = logging::ll_error;

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
		FILE *lfh = fopen(logfile, "a+");
		if (!lfh) {
			fprintf(stderr, "Cannot access log-file \"%s\": %s\n", logfile, strerror(errno));
			exit(1);
		}

		uint64_t now   = get_micros();
		time_t   t_now = now / 1000000;

#if defined(__MINGW32__)
		tm tm = *localtime(&t_now);
#else
		tm tm { };
		if (!localtime_r(&t_now, &tm))
			fprintf(stderr, "localtime_r: %s\n", strerror(errno));
#endif

		const char *const ll_names[] = { "debug", "info", "warning", "error" };

#if defined(RP2040W)
		char ts_str[48] { };
		snprintf(ts_str, sizeof ts_str, "%04d-%02d-%02d %02d:%02d:%02d.%06d %s | %s | %s | ",
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, int(now % 1000000),
				ll_names[ll], component, context.c_str());

		char str[128];
		va_list ap;
		va_start(ap, fmt);
		(void)vsnprintf(str, sizeof str, fmt, ap);
		va_end(ap);
#else
		char *ts_str = nullptr;
		if (asprintf(&ts_str, "%04d-%02d-%02d %02d:%02d:%02d.%06d %s | %s | %s | ",
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, int(now % 1000000),
				ll_names[ll], component, context.c_str()) == -1)
			ts_str = strdup("[???]");

		char *str = nullptr;
		va_list ap;
		va_start(ap, fmt);
		if (vasprintf(&str, fmt, ap) == -1)
			str = strdup(fmt);
		va_end(ap);
#endif

		if (ll >= log_level_file)
			fprintf(lfh, "%s%s\n", ts_str, str);

		if (ll >= log_level_screen)
			printf("%s%s\n", ts_str, str);

#if !defined(RP2040W)
		free(str);
		free(ts_str);
#endif

		fclose(lfh);
	}
}
#endif
