#include <cinttypes>
#include <cstring>
#include <optional>

#include <fcntl.h>
#include <unistd.h>

#if defined(ESP32)
#include <esp_heap_caps.h>
#endif

#include "backend-cache.h"
#include "gen.h"
#include "log.h"
#include "utils.h"


backend_cache::backend_cache(backend *const parent_in, const size_t max_n_bytes):
	backend("cache"),
	parent(parent_in),
	max_n_bytes(max_n_bytes)
{
}

backend_cache::~backend_cache()
{
#if !defined(TEENSY4_1)
	if (prefetcher) {
		stop_flag = true;
		prefetcher->join();
		delete prefetcher;
	}
#endif

	delete c;
}

bool backend_cache::begin()
{
	size_t n_elements = max_n_bytes / parent->get_block_size();
	if (n_elements) {
		c = new cache(n_elements);
#if defined(TEENSY4_1)
		return true;
#else
		if (parent->begin()) {
			prefetcher = new std::thread([&] { prefetcher_thread(); });
			return true;
		}
		delete c;
		c = nullptr;
#endif
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

void backend_cache::invalidate_range(const uint64_t block_nr, const uint32_t n_blocks)
{
	for(uint32_t i=0; i<n_blocks; i++)
		c->invalidate(block_nr + i);
}

uint8_t *backend_cache::duplicate_block(const uint8_t *const p, const size_t s)
{
#if defined(ESP32)
	uint8_t *dest = reinterpret_cast<uint8_t *>(heap_caps_malloc(s, MALLOC_CAP_SPIRAM));
#else
	uint8_t *dest = reinterpret_cast<uint8_t *>(malloc(s));
#endif
	if (dest)
		memcpy(dest, p, s);
	return dest;
}

bool backend_cache::write(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data)
{
	std::unique_lock<std::mutex> lck(cache_lock);
	auto block_size = get_block_size();
	auto rc         = parent->write(block_nr, n_blocks, data);
	if (rc == true) {
		for(uint32_t i=0; i<n_blocks; i++)
			c->put(block_nr + i, duplicate_block(&data[i * block_size], block_size));
		return true;
	}
	invalidate_range(block_nr, n_blocks);
	return false;
}

bool backend_cache::trim(const uint64_t block_nr, const uint32_t n_blocks)
{
	std::unique_lock<std::mutex> lck(cache_lock);
	if (parent->trim(block_nr, n_blocks)) {
		for(uint32_t i=0; i<n_blocks; i++)
			c->invalidate(block_nr + i);
		return true;
	}
	invalidate_range(block_nr, n_blocks);
	return false;
}

#if !defined(TEENSY4_1)
void backend_cache::prefetcher_thread()
{
	auto     block_size = get_block_size();
	uint8_t *buffer     = new uint8_t[block_size];
	uint32_t current_id {};
	while(!stop_flag) {
		std::optional<uint64_t> current_block;
		{
			bool fetch {};
			std::unique_lock<std::mutex> lck(prefetch_lock);
			if (current_id == prefetch_id) {
				if (prefetch_count == 0)
					prefetch_cv.wait_for(lck, std::chrono::milliseconds(100));
				else
					fetch = true;
			}
			else {
				current_id = prefetch_id;
				fetch      = true;
			}
			if (fetch) {
				current_block = prefetch_block_nr++;
				prefetch_count--;
				DOLOG(logging::ll_debug, "backend_cache", identifier, "prefetch %" PRIu64, current_block.value());
			}
		}

		if (current_block.has_value() == false)
			continue;

		std::unique_lock<std::mutex> lck(cache_lock);
		if (parent->read(current_block.value(), 1, buffer) == true)
			c->put(current_block.value(), duplicate_block(buffer, block_size));
		else {
			lck.unlock();
			DOLOG(logging::ll_error, "backend_cache", identifier, "cannot read from parent");
		}
	}

	delete [] buffer;
}
#endif

bool backend_cache::prefetch(const uint64_t block_nr, const uint32_t n_blocks)
{
#if defined(TEENSY4_1)
	return true
#else
	std::unique_lock<std::mutex> lck(prefetch_lock);
	prefetch_id++;
	prefetch_block_nr = block_nr;
	prefetch_count    = n_blocks;
	prefetch_cv.notify_one();
	return !!prefetcher;
#endif
}

bool backend_cache::read(const uint64_t block_nr, const uint32_t n_blocks, uint8_t *const data)
{
	std::unique_lock<std::mutex> lck(cache_lock);
	auto block_size = get_block_size();
	for(uint32_t i=0; i<n_blocks; i++) {
		auto *p = c->get(block_nr + i);
		if (p)
			memcpy(&data[i * block_size], p, block_size);
		else {
			if (parent->read(block_nr + i, 1, &data[i * block_size]) == false)
				return false;

			c->put(block_nr + i, duplicate_block(&data[i * block_size], block_size));
		}
	}
	return true;
}

backend::cmpwrite_result_t backend_cache::cmpwrite(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data_write, const uint8_t *const data_compare)
{
	std::unique_lock<std::mutex> lck(cache_lock);
	auto block_size = get_block_size();
	auto rc         = parent->cmpwrite(block_nr, n_blocks, data_write, data_compare);
	if (rc == cmpwrite_result_t::CWR_OK) {
                for(uint32_t i=0; i<n_blocks; i++)
                        c->put(block_nr + i, duplicate_block(&data_write[i * block_size], block_size));
		return cmpwrite_result_t::CWR_OK;
	}
	invalidate_range(block_nr, n_blocks);
	return rc;
}

std::string backend_cache::get_serial() const
{
	return parent->get_serial();
}
