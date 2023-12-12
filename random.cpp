#ifdef linux
#include <cstdint>
#include <sys/random.h>

void my_getrandom(void *const tgt, const size_t n)
{
	uint8_t *workp = reinterpret_cast<uint8_t *>(tgt);
	size_t   todo  = n;

	while(todo > 0) {
		ssize_t rc = getrandom(workp, todo, 0);

		if (rc == -1) {
			// TODO
		}

		workp += rc;
		todo  -= rc;
	}
}
#else
#include <esp_random.h>

void my_getrandom(void *const tgt, const size_t n)
{
	esp_fill_random(tgt, n);
}
#endif
