#pragma once
#include <cstdint>
#if !(defined(Arduino) || defined(TEENSY4_1))
#include <array>
#include <mutex>
#endif
#include <vector>


#if defined(ESP32)
#define N_BACKEND_LOCKS 4
#else
#define N_BACKEND_LOCKS 131
#endif

class backend
{
protected:
	uint64_t bytes_read    { 0 };
	uint64_t bytes_written { 0 };
	uint64_t n_syncs       { 0 };
	uint64_t n_trims       { 0 };
	uint64_t ts_last_acces { 0 };

#if !(defined(Arduino) || defined(TEENSY4_1))
	std::array<std::mutex, N_BACKEND_LOCKS> locks;
#endif

	std::vector<size_t> lock_range  (const uint64_t block_nr, const uint32_t block_n);
	void                unlock_range(const std::vector<size_t> & locked_locks);

public:
	backend();
	virtual ~backend();

	virtual bool begin() = 0;

	virtual uint64_t get_size_in_blocks() const = 0;
	virtual uint64_t get_block_size()     const = 0;

	// mainly for thin provisioning
	virtual uint8_t get_free_space_percentage();

	virtual bool is_idle();

	virtual bool sync() = 0;

	void get_and_reset_stats(uint64_t *const bytes_read, uint64_t *const bytes_written, uint64_t *const n_syncs, uint64_t *const n_trims);

	enum cmpwrite_result_t { CWR_OK, CWR_MISMATCH, CWR_READ_ERROR, CWR_WRITE_ERROR };

	virtual bool write   (const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data) = 0;
	virtual bool trim    (const uint64_t block_nr, const uint32_t n_blocks                           ) = 0;
	virtual bool read    (const uint64_t block_nr, const uint32_t n_blocks,       uint8_t *const data) = 0;
	virtual backend::cmpwrite_result_t cmpwrite(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data_write, const uint8_t *const data_compare) = 0;
};
