#pragma once
#include <cstdint>


class backend
{
public:
	backend();
	virtual ~backend();

	virtual uint64_t get_size_in_blocks() const = 0;
	virtual uint64_t get_block_size()     const = 0;

	virtual bool write(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data) = 0;
	virtual bool read (const uint64_t block_nr, const uint32_t n_blocks,       uint8_t *const data) = 0;
};
