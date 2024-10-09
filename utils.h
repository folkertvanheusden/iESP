#pragma once

#include <cstdarg>
#include <cstdint>
#include <string>
#include <vector>

#if defined(__MINGW32__)
// https://stackoverflow.com/questions/40159892/using-asprintf-on-windows
#ifndef _vscprintf
/* For some reason, MSVC fails to honour this #ifndef. */
/* Hence function renamed to _vscprintf_so(). */
int _vscprintf_so(const char * format, va_list pargs);
#endif // _vscprintf
#ifndef vasprintf
int vasprintf(char **strp, const char *fmt, va_list ap);
#endif // vasprintf
#ifndef asprintf
int asprintf(char *strp[], const char *fmt, ...);
#endif // asprintf
#endif

ssize_t READ (const int fd, uint8_t *whereto, size_t len);
ssize_t WRITE(const int fd, const uint8_t *whereto, size_t len);
std::vector<std::string> split(std::string in, const std::string & splitter);
std::string myformat(const char *const fmt, ...);
uint32_t get_free_heap_space();
uint64_t get_uint64_t(const uint8_t *const p);
uint32_t get_uint32_t(const uint8_t *const p);
uint64_t get_micros();
uint64_t get_millis();
void teensyMAC(uint8_t *const mac);
void encode_lun(uint8_t *const target, const uint64_t lun_nr);

uint8_t * duplicate_new(const void *const in, const size_t n);

std::string to_hex(const uint8_t *const in, const size_t n);

// Apple has NTOHL/etc macros that confuse the build
uint32_t my_NTOHL(const uint32_t x);
uint16_t my_NTOHS(const uint16_t x);
uint32_t my_HTONL(const uint32_t x);
uint16_t my_HTONS(const uint16_t x);

void socket_set_nodelay(const int fd);

#define my_HTONLL(x) ((1==my_HTONL(1)) ? (x) : (((uint64_t)my_HTONL((x) & 0xFFFFFFFFUL)) << 32) | my_HTONL((uint32_t)((x) >> 32)))
#define my_NTOHLL(x) ((1==my_NTOHL(1)) ? (x) : (((uint64_t)my_NTOHL((x) & 0xFFFFFFFFUL)) << 32) | my_NTOHL((uint32_t)((x) >> 32)))

extern uint64_t running_since;
