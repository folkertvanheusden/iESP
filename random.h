#include <cstddef>
#include <cstdint>

void init_my_getrandom();
void my_getrandom(void *const tgt, const size_t n);
uint32_t my_getrandom();
