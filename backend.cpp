#include <cstring>

#include "backend.h"
#include "random.h"
#include "utils.h"


backend::backend()
{
	ts_last_acces = get_micros();
}

backend::~backend()
{
}

void backend::get_and_reset_stats(uint64_t *const bytes_read, uint64_t *const bytes_written, uint64_t *const n_syncs, uint64_t *const n_trims)
{
	*bytes_read    = this->bytes_read;
	*bytes_written = this->bytes_written;
	*n_syncs       = this->n_syncs;
	*n_trims       = this->n_trims;

	this->bytes_read    = 0;
	this->bytes_written = 0;
	this->n_syncs       = 0;
	this->n_trims       = 0;
}

bool backend::is_idle()
{
	uint64_t now = get_micros();

	return now - ts_last_acces >= 499000;  // about halve a second ago?
}

uint8_t backend::get_free_space_percentage()
{
	auto     size  = get_size_in_blocks();
	uint64_t th100 = size / 100;
	uint8_t  empty_count = 0;

	if (th100 == 0)
		return 0;

	uint8_t  *buffer = new uint8_t[512]();
	uint8_t  *empty  = new uint8_t[512]();

	for(int i=0; i<100; i++) {
		uint64_t block_nr = 0;

		// random in case a filesystem or whatever places static data at every 100th position
		do {
			block_nr = ((uint64_t(my_getrandom()) << 32) | my_getrandom()) % size;
			block_nr += int32_t(my_getrandom() % (th100 * 2)) - th100;
		}
		while(block_nr >= size);

		if (read(block_nr, 1, buffer) == false) {
			empty_count = 0;
			break;
		}

		if (memcmp(buffer, empty, 512) == 0)
			empty_count++;
	}

	delete [] buffer;
	delete [] empty;

	return empty_count;
}
