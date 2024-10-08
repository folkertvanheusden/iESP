#if defined(__MINGW32__)
#include <cstdint>
#include <cstdlib>
#include <ctime>

void init_my_getrandom()
{
	srand(time(nullptr));
}

bool my_getrandom(void *const tgt, const size_t n)
{
	uint8_t *workp = reinterpret_cast<uint8_t *>(tgt);
	for(size_t i=0; i<n; i++)
		workp[i] = rand();  // hope for the best!

	return true;
}
#elif defined(linux) || defined(__FreeBSD__)
#include <cstdint>
#include <sys/random.h>

void init_my_getrandom()
{
}

bool my_getrandom(void *const tgt, const size_t n)
{
	uint8_t *workp = reinterpret_cast<uint8_t *>(tgt);
	size_t   todo  = n;

	while(todo > 0) {
		ssize_t rc = getrandom(workp, todo, 0);
		if (rc == -1)
			return false;

		workp += rc;
		todo  -= rc;
	}

	return true;
}
#elif defined(RP2040W)
#include <cstdint>
#include <cstdlib>
#include <pico/rand.h>

void init_my_getrandom()
{
}

bool my_getrandom(void *const tgt, const size_t n)
{
	for(size_t i=0; i<n; i++)
		reinterpret_cast<uint8_t *>(tgt)[i] = get_rand_32();

	return true;
}
#elif defined(ESP32)
#include <esp_random.h>

void init_my_getrandom()
{
}

bool my_getrandom(void *const tgt, const size_t n)
{
	esp_fill_random(tgt, n);
	return true;
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

bool my_getrandom(void *const tgt, const size_t n)
{
	for(size_t i=0; i<n; i++)
		reinterpret_cast<uint8_t *>(tgt)[i] = ec.random();

	return true;
}
#endif

bool my_getrandom(uint32_t *const v)
{
	return my_getrandom(v, sizeof *v);
}
