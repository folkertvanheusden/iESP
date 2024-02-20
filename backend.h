#pragma once
#include <cstdint>


class backend
{
protected:
	uint64_t bytes_read    { 0 };
	uint64_t bytes_written { 0 };
	uint64_t n_syncs       { 0 };

public:
	backend();
	virtual ~backend();

	virtual bool begin() = 0;

	virtual uint64_t get_size_in_blocks() const = 0;
	virtual uint64_t get_block_size()     const = 0;

	virtual bool sync() = 0;

	void get_and_reset_stats(uint64_t *const bytes_read, uint64_t *const bytes_written, uint64_t *const n_syncs);

	virtual bool write(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data) = 0;
	virtual bool trim (const uint64_t block_nr, const uint32_t n_blocks                           ) = 0;
	virtual bool read (const uint64_t block_nr, const uint32_t n_blocks,       uint8_t *const data) = 0;
};
