#include <cstdint>
#include <string>
#include <vector>

ssize_t READ (const int fd, uint8_t *whereto, size_t len);
ssize_t WRITE(const int fd, const uint8_t *whereto, size_t len);
std::vector<std::string> split(std::string in, const std::string & splitter);
std::string myformat(const char *const fmt, ...);
uint32_t get_free_heap_space();
uint64_t get_uint64_t(const uint8_t *const p);
uint32_t get_uint32_t(const uint8_t *const p);
uint64_t get_micros();
void teensyMAC(uint8_t *const mac);
void encode_lun(uint8_t *const target, const uint64_t lun_nr);

#if !defined(ARDUINO)
std::string to_hex(const uint8_t *const in, const size_t n);
#endif

uint32_t NTOHL(const uint32_t x);
uint16_t NTOHS(const uint16_t x);
uint32_t HTONL(const uint32_t x);
uint16_t HTONS(const uint16_t x);

extern uint64_t running_since;
