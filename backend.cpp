#include <cstring>

#include "backend.h"
#include "log.h"
#include "random.h"
#include "utils.h"


backend::backend(const std::string & identifier): identifier(identifier)
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
	auto     block_size  = get_block_size();
	auto     size        = get_size_in_blocks();
	uint64_t th100       = size / 100;
	uint8_t  empty_count = 0;

	if (th100 == 0)
		return 0;

	uint8_t  *buffer = new uint8_t[block_size]();
	uint8_t  *empty  = new uint8_t[block_size]();

	for(int i=0; i<100; i++) {
		uint64_t block_nr = 0;

		// random in case a filesystem or whatever places static data at every xth position
		uint64_t rnd = 0;
		if (my_getrandom(&rnd, sizeof rnd) == false) {
			DOLOG(logging::ll_error, "random", identifier, "random generator returned an error");
			empty_count = 0;
			break;
		}
		block_nr = rnd % size;

		auto rc = read(block_nr, 1, buffer);
		if (rc == false) {
			empty_count = 0;
			break;
		}

		if (memcmp(buffer, empty, block_size) == 0)
			empty_count++;
	}

	delete [] buffer;
	delete [] empty;

	return empty_count;
}

std::set<size_t> backend::lock_range(const uint64_t block_nr, const uint32_t block_n)
{
#if defined(ARDUINO) || defined(TEENSY4_1)
	return { };  // no-op
#else
	std::set<size_t> indexes;

	for(uint64_t i=block_nr; i<block_nr + block_n; i++)
		indexes.insert(size_t((i * LOCK_SPREADER) % N_BACKEND_LOCKS));

	for(auto nr: indexes)
		locks[nr].lock();

	return indexes;
#endif
}

void backend::unlock_range(const std::set<size_t> & locked_locks)
{
#if defined(ARDUINO) || defined(TEENSY4_1)
	// no-op
#else
	for(auto nr: locked_locks)
		locks[nr].unlock();
#endif
}
