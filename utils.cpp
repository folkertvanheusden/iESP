#include <cstdint>
#include <string>
#include <unistd.h>
#include <vector>


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

std::vector<std::string> split(std::string in, std::string splitter)
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
