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
#include <hardware/regs/addressmap.h>
#include <hardware/regs/rosc.h>

void init_my_getrandom()
{
	uint32_t random = 0;
	uint32_t random_bit;
	volatile uint32_t *const rnd_reg = (uint32_t *)(ROSC_BASE + ROSC_RANDOMBIT_OFFSET);

	for(int k = 0; k < 32; k++) {
		do {
			random_bit = (*rnd_reg) & 1;
		}
		while(random_bit == ((*rnd_reg) & 1));

		random = (random << 1) | random_bit;
	}

	srand(random);
}

void my_getrandom(void *const tgt, const size_t n)
{
	// TODO need to improve this
	for(size_t i=0; i<n; i++)
		reinterpret_cast<uint8_t *>(tgt)[i] = rand();
}
#else
#include <esp_random.h>

void init_my_getrandom()
{
}

void my_getrandom(void *const tgt, const size_t n)
{
	esp_fill_random(tgt, n);
}
#endif
