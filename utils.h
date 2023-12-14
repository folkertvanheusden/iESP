#include <cstdint>
#include <string>
#include <vector>

ssize_t READ (const int fd, uint8_t *whereto, size_t len);
ssize_t WRITE(const int fd, const uint8_t *whereto, size_t len);
std::vector<std::string> split(std::string in, std::string splitter);
