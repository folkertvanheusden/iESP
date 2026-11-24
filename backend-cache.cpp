#include <cinttypes>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include "backend-cache.h"
#include "gen.h"
#include "log.h"
#include "utils.h"


backend_cache::backend_cache(backend *const parent_in, const size_t max_n_bytes):
	backend(parent_in->get_serial()),
	parent(parent_in),
	max_n_bytes(max_n_bytes)
{
}

backend_cache::~backend_cache()
{
	delete c;
}

bool backend_cache::begin()
{
	size_t n_elements = max_n_bytes / parent->get_block_size();
	if (n_elements) {
		c = new cache(n_elements);
		return parent->begin();
	}
	return false;
}

uint64_t backend_cache::get_size_in_blocks() const
{
	return parent->get_size_in_blocks();
}

uint64_t backend_cache::get_block_size() const
{
	return parent->get_block_size();
}

bool backend_cache::sync()
{
	return parent->sync();
}

bool backend_cache::write(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data)
{
	auto block_size = get_block_size();
	auto rc         = parent->write(block_nr, n_blocks, data);
	if (rc == true) {
		for(uint32_t i=0; i<n_blocks; i++)
			c->put(block_nr + i, duplicate_new(&data[i * block_size], block_size));
		return true;
	}
	return false;
}

bool backend_cache::trim(const uint64_t block_nr, const uint32_t n_blocks)
{
	if (parent->trim(block_nr, n_blocks)) {
		for(uint32_t i=0; i<n_blocks; i++)
			c->invalidate(block_nr + i);
		return true;
	}
	return false;
}

bool backend_cache::read(const uint64_t block_nr, const uint32_t n_blocks, uint8_t *const data)
{
	auto block_size = get_block_size();
	for(uint32_t i=0; i<n_blocks; i++) {
		auto *p = c->get(block_nr + i);
		if (p)
			memcpy(&data[i * block_size], p, block_size);
		else {
			if (parent->read(block_nr + i, 1, &data[i * block_size]) == false)
				return false;

			c->put(block_nr + i, duplicate_new(&data[i * block_size], block_size));
		}
	}
	return true;
}

backend::cmpwrite_result_t backend_cache::cmpwrite(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data_write, const uint8_t *const data_compare)
{
	auto block_size = get_block_size();
	auto rc         = parent->cmpwrite(block_nr, n_blocks, data_write, data_compare);
	if (rc == cmpwrite_result_t::CWR_OK) {
                for(uint32_t i=0; i<n_blocks; i++)
                        c->put(block_nr + i, duplicate_new(&data_write[i * block_size], block_size));
		return cmpwrite_result_t::CWR_OK;
	}
	return rc;
}

std::string backend_cache::get_serial() const
{
	return parent->get_serial();
}
