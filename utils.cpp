#include <cstdint>
#include <unistd.h>


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
		len -= rc;
	}

	return cnt;
}
