#ifdef linux
#include <cstdio>
#include <ctime>
#include <sys/time.h>
#define DOLOG(fmt, ...) do {                \
		FILE *fh = fopen("log.dat", "a+"); \
		if (fh) {                          \
			timeval tv;          \
			gettimeofday(&tv, nullptr);\
			struct tm tm;        \
			localtime_r(&tv.tv_sec, &tm);\
			fprintf(fh, "%04d-%02d-%02d %02d:%02d:%02d.%06d ", \
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, int(tv.tv_usec)); \
			fprintf(fh, fmt, ##__VA_ARGS__); \
			fclose(fh);         \
		}                           \
                printf(fmt, ##__VA_ARGS__); \
        } while(0)
#else
#define DOLOG(fmt, ...) do { } while(0)
#endif
