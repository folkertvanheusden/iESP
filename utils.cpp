#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>
#if defined(RP2040W) || defined(ARDUINO)
#include <Arduino.h>
#else
#include <netdb.h>
#include <time.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif
#include <sys/types.h>

#include "log.h"


ssize_t READ(const int fd, uint8_t *whereto, size_t len)
{
	ssize_t cnt = len;

	while(len > 0)
	{
		ssize_t rc = read(fd, whereto, len);

		if (rc <= 0)
			return -1;

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

#if !defined(ARDUINO)
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
#endif

std::string myformat(const char *const fmt, ...)
{
#if !defined(ARDUINO)
        char *buffer = nullptr;
        va_list ap;
        va_start(ap, fmt);
        if (vasprintf(&buffer, fmt, ap) == -1) {
                va_end(ap);
                errlog("myformat: failed to convert string with format \"%s\"", fmt);
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
                errlog("myformat: failed to convert string with format \"%s\"", fmt);
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
		errlog("get_micros: clock_gettime failed (%s)", strerror(errno));
		return 0;
	}

        return uint64_t(tv.tv_sec) * uint64_t(1000 * 1000) + uint64_t(tv.tv_nsec / 1000);
#endif
}

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
