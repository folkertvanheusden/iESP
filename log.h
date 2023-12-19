#ifdef linux
#define DOLOG(fmt, ...) do {                \
                printf(fmt, ##__VA_ARGS__); \
        } while(0)
#else
#define DOLOG(ll, always, fmt, ...) do { } while(0)
#endif
