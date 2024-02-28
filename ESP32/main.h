#include <cstdint>
#include <vector>


extern std::vector<std::pair<std::string, std::string> > wifi_targets;
extern int led_green;
extern int led_yellow;
extern int led_red;


void draw_status(const uint32_t v);
void write_led(const int gpio, const int state);
