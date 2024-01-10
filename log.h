#ifdef linux
#define DOLOG(fmt, ...) do {                \
		FILE *fh = fopen("log.dat", "a+"); \
		if (fh) {                   \
			fprintf(fh, fmt, ##__VA_ARGS__); \
			fclose(fh);         \
		}                           \
                printf(fmt, ##__VA_ARGS__); \
        } while(0)
#else
#define DOLOG(ll, fmt, ...) do { } while(0)
#endif
