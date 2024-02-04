#include <cstdint>
#include <string>
#include <vector>

ssize_t READ (const int fd, uint8_t *whereto, size_t len);
ssize_t WRITE(const int fd, const uint8_t *whereto, size_t len);
std::vector<std::string> split(std::string in, const std::string & splitter);
std::string myformat(const char *const fmt, ...);
std::string get_endpoint_name(int fd);

#ifdef linux
std::string to_hex(const uint8_t *const in, const size_t n);
#endif
