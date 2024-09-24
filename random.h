#include <cstddef>
#include <cstdint>

void init_my_getrandom();
bool my_getrandom(void *const tgt, const size_t n);
bool my_getrandom(uint32_t *const v);
