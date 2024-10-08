#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>
#if defined(RP2040W) || defined(ARDUINO)
#include <Arduino.h>
#elif defined(__MINGW32__)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif
#include <time.h>
#include <sys/types.h>
#if !defined(__MINGW32__)
#if !defined(TEENSY4_1)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif
#endif

#include "log.h"
#include "utils.h"


ssize_t READ(const int fd, uint8_t *whereto, size_t len)
{
	ssize_t cnt = len;

	while(len > 0)
	{
		ssize_t rc = read(fd, whereto, len);
		if (rc <= 0)
			return rc;

		whereto += rc;
		len -= rc;
	}

	return cnt;
}

ssize_t WRITE(const int fd, const uint8_t *whereto, size_t len)
{
	ssize_t cnt = len;

	while(len > 0) {
		ssize_t rc = write(fd, whereto, len);
		if (rc <= 0)
			return -1;

		whereto += rc;
		len     -= rc;
	}

	return cnt;
}

std::vector<std::string> split(std::string in, const std::string & splitter)
{
	std::vector<std::string> out;
	size_t splitter_size = splitter.size();

	for(;;)
	{
		size_t pos = in.find(splitter);
		if (pos == std::string::npos)
			break;

		std::string before = in.substr(0, pos);
		out.push_back(before);

		size_t bytes_left = in.size() - (pos + splitter_size);
		if (bytes_left == 0)
		{
			out.push_back("");
			return out;
		}

		in = in.substr(pos + splitter_size);
	}

	if (in.size() > 0)
		out.push_back(in);

	return out;
}

std::string to_hex(const uint8_t *const in, const size_t n)
{
	std::string out;
	out.resize(n * 3);

	for(size_t i=0, o = 0; i<n; i++) {
		uint8_t v = in[i];
		uint8_t nh = v >> 4;
		uint8_t nl = v & 15;

		if (i)
			out.at(o++) = ' ';

		if (nh > 9)
			out.at(o++) = 'a' + nh - 10;
		else
			out.at(o++) = '0' + nh;

		if (nl > 9)
			out.at(o++) = 'a' + nl - 10;
		else
			out.at(o++) = '0' + nl;
	}

	return out;
}

std::string myformat(const char *const fmt, ...)
{
#if !defined(ARDUINO)
        char *buffer = nullptr;
        va_list ap;
        va_start(ap, fmt);
        if (vasprintf(&buffer, fmt, ap) == -1) {
                va_end(ap);
		// possible recursive error
                DOLOG(logging::ll_error, "myformat", "-", "failed to convert string with format \"%s\"", fmt);
                return fmt;
        }
        va_end(ap);

        std::string result = buffer;
        free(buffer);

        return result;
#else
	char buffer[256];

        va_list ap;
        va_start(ap, fmt);
        if (vsnprintf(buffer, sizeof buffer, fmt, ap) == -1) {
                va_end(ap);
                DOLOG(logging::ll_error, "myformat", "-", "failed to convert string with format \"%s\"", fmt);
                return fmt;
        }
        va_end(ap);

        std::string result = buffer;

        return result;
#endif
}

uint32_t get_free_heap_space()
{
#if defined(RP2040W)
	return rp2040.getFreeHeap();
#elif defined(ESP32)
	return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
#else
	return 0;
#endif
}

uint64_t get_uint64_t(const uint8_t *const p)
{
	return (uint64_t(p[0]) << 56) | (uint64_t(p[1]) << 48) | (uint64_t(p[2]) << 40) | (uint64_t(p[3]) << 32) | (uint64_t(p[4]) << 24) | (p[5] << 16) | (p[6] << 8) | p[7];
}

uint32_t get_uint32_t(const uint8_t *const p)
{
	return (uint64_t(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

uint64_t get_micros()
{
#if defined(ARDUINO)
	return micros();
#else
        struct timespec tv { };
        if (clock_gettime(CLOCK_REALTIME, &tv) == -1) {
		DOLOG(logging::ll_error, "get_micros", "-", "clock_gettime failed: %s", strerror(errno));
		return 0;
	}

        return uint64_t(tv.tv_sec) * uint64_t(1000 * 1000) + uint64_t(tv.tv_nsec / 1000);
#endif
}

uint64_t get_millis()
{
#if defined(ARDUINO)
	return millis();
#else
        struct timespec tv { };
        if (clock_gettime(CLOCK_REALTIME, &tv) == -1) {
		DOLOG(logging::ll_error, "get_millis", "-", "clock_gettime failed: %s", strerror(errno));
		return 0;
	}

        return uint64_t(tv.tv_sec) * uint64_t(1000) + uint64_t(tv.tv_nsec / 1000000);
#endif
}

#if defined(TEENSY4_1)
void teensyMAC(uint8_t *const mac)
{
	uint32_t m1 = HW_OCOTP_MAC1;
	uint32_t m2 = HW_OCOTP_MAC0;
	mac[0] = m1 >> 8;
	mac[1] = m1 >> 0;
	mac[2] = m2 >> 24;
	mac[3] = m2 >> 16;
	mac[4] = m2 >> 8;
	mac[5] = m2 >> 0;
}
#endif

uint64_t running_since = get_micros();

void encode_lun(uint8_t *const target, const uint64_t lun_nr)
{
	memset(target, 0x00, sizeof(uint64_t));

	if (lun_nr < 256) {
		target[0] = 0;
		target[1] = lun_nr;
	}
	else if (lun_nr < 0x4000) {
		target[0] = 1;
		target[1] = lun_nr >> 8;
		target[2] = lun_nr;
	}
	else {
		memcpy(target, &lun_nr, 8);  // TODO
	}
}

uint16_t HTONS(const uint16_t x)
{
	constexpr const uint16_t e = 1;
	if (*reinterpret_cast<const uint8_t *>(&e))  // system is little endian
		return (x << 8) | (x >> 8);

	return x;
}

uint32_t HTONL(const uint32_t x)
{
	constexpr const uint16_t e = 1;
	if (*reinterpret_cast<const uint8_t *>(&e))  // system is little endian
		return (HTONS(x) << 16) | HTONS(x >> 16);

	return x;
}

uint16_t NTOHS(const uint16_t x)
{
	return HTONS(x);
}

uint32_t NTOHL(const uint32_t x)
{
	return HTONL(x);
}

uint8_t * duplicate_new(const void *const in, const size_t n)
{
	uint8_t *out = new uint8_t[n];
	memcpy(out, in, n);
	return out;
}

#if defined(__MINGW32__)
// https://stackoverflow.com/questions/40159892/using-asprintf-on-windows
#ifndef _vscprintf
/* For some reason, MSVC fails to honour this #ifndef. */
/* Hence function renamed to _vscprintf_so(). */
int _vscprintf_so(const char * format, va_list pargs) {
    int retval;
    va_list argcopy;
    va_copy(argcopy, pargs);
    retval = vsnprintf(NULL, 0, format, argcopy);
    va_end(argcopy);
    return retval;}
#endif // _vscprintf

#ifndef vasprintf
int vasprintf(char **strp, const char *fmt, va_list ap) {
    int len = _vscprintf_so(fmt, ap);
    if (len == -1) return -1;
    char *str = (char *)malloc((size_t) len + 1);
    if (!str) return -1;
    int r = vsnprintf(str, len + 1, fmt, ap); /* "secure" version of vsprintf */
    if (r == -1) return free(str), -1;
    *strp = str;
    return r;}
#endif // vasprintf

#ifndef asprintf
int asprintf(char *strp[], const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vasprintf(strp, fmt, ap);
    va_end(ap);
    return r;}
#endif // asprintf
#endif

void socket_set_nodelay(const int fd)
{
#if !defined(TEENSY4_1)
	int flags = 1;
#if defined(__FreeBSD__) || defined(ESP32) || defined(__MINGW32__)
	if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&flags, sizeof(flags)) == -1)
#else
	if (setsockopt(fd, SOL_TCP, TCP_NODELAY, (void *)&flags, sizeof(flags)) == -1)
#endif
#endif
		DOLOG(logging::ll_error, "com_client_sockets", "-", "cannot disable Nagle algorithm");
}
