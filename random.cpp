#if defined(linux) || defined(__FreeBSD__)
#include <cstdint>
#include <sys/random.h>

void init_my_getrandom()
{
}

void my_getrandom(void *const tgt, const size_t n)
{
	uint8_t *workp = reinterpret_cast<uint8_t *>(tgt);
	size_t   todo  = n;

	while(todo > 0) {
		ssize_t rc = getrandom(workp, todo, 0);

		if (rc == -1) {
			// TODO
		}
		else {
			workp += rc;
			todo  -= rc;
		}
	}
}
#elif defined(RP2040W)
#include <cstdint>
#include <cstdlib>
#include <pico/rand.h>

void init_my_getrandom()
{
}

void my_getrandom(void *const tgt, const size_t n)
{
	size_t i=0;
	for(; i<n; i += 4)
		reinterpret_cast<uint32_t *>(tgt)[i] = get_rand_32();
	while(i<n)
		reinterpret_cast<uint8_t *>(tgt)[i++] = get_rand_32();
}
#elif defined(ESP32)
#include <esp_random.h>

void init_my_getrandom()
{
}

void my_getrandom(void *const tgt, const size_t n)
{
	esp_fill_random(tgt, n);
}
#elif defined(TEENSY4_1)
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <Entropy.h>

EntropyClass ec;

void init_my_getrandom()
{
	ec.Initialize();
}

void my_getrandom(void *const tgt, const size_t n)
{
	for(size_t i=0; i<n; i++) {
		reinterpret_cast<uint8_t *>(tgt)[i] = ec.random();  // TODO 32bit at a time
	}
}
#endif
