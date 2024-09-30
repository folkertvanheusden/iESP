#include <string>

#if defined(ARDUINO)
void initlogger();
#define DOLOG(ll, component, context, fmt, ...) do { } while(0)
#else
namespace logging {
        typedef enum { ll_debug, ll_info, ll_warning, ll_error } log_level_t;

        extern log_level_t log_level_file, log_level_screen;

	void initlogger();
        void setlog(const char *lf, const log_level_t ll_file, const log_level_t ll_screen);
        void dolog (const logging::log_level_t ll, const char *const component, const std::string context, const char *fmt, ...);

#define DOLOG(ll, component, context, fmt, ...) do {                            \
                if (ll >= logging::log_level_file || ll >= logging::log_level_screen)           \
                        logging::dolog(ll, component, context, fmt, ##__VA_ARGS__);     \
        } while(0)
}
#endif

#include <cstdarg>
void errlog(const char *const fmt, ...);
#if defined(ARDUINO)
#include <optional>
#include <string>
extern std::optional<std::string> syslog_host;
#endif
void init_logger(const std::string & name);
