#include <cstdarg>
#include <cstdint>
#include <string>
#include <unistd.h>
#include <vector>
#if defined(RP2040W)
#include <Arduino.h>
#else
#include <netdb.h>
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

#ifdef linux
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
#ifdef linux
        char *buffer = nullptr;
        va_list ap;
        va_start(ap, fmt);
        if (vasprintf(&buffer, fmt, ap) == -1) {
                va_end(ap);
                DOLOG("myformat: failed to convert string with format \"%s\"\n", fmt);
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
                DOLOG("myformat: failed to convert string with format \"%s\"\n", fmt);
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
