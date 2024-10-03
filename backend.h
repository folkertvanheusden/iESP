#pragma once
#include <cstdint>
#if !(defined(ARDUINO) || defined(TEENSY4_1))
#include <array>
#include <mutex>
#endif
#include <set>
#include <string>


#if defined(ESP32)
#define N_BACKEND_LOCKS 4
#define LOCK_SPREADER 3
#else
#define N_BACKEND_LOCKS 131
#define LOCK_SPREADER 31
#endif

class backend
{
protected:
	const std::string identifier;

	uint64_t bytes_read    { 0 };
	uint64_t bytes_written { 0 };
	uint64_t n_syncs       { 0 };
	uint64_t n_trims       { 0 };
	uint64_t ts_last_acces { 0 };

#if !(defined(ARDUINO) || defined(TEENSY4_1) || defined(RP2040W))
	std::array<std::mutex, N_BACKEND_LOCKS> locks;
#endif

	std::set<size_t> lock_range  (const uint64_t block_nr, const uint32_t block_n);
	void             unlock_range(const std::set<size_t> & locked_locks);

public:
	backend(const std::string & identifier);
	virtual ~backend();

	virtual bool begin() = 0;

	virtual uint64_t get_size_in_blocks() const = 0;
	virtual uint64_t get_block_size()     const = 0;

	// mainly for thin provisioning
	virtual uint8_t get_free_space_percentage();

	virtual std::pair<uint64_t, uint32_t> get_idle_state();

	virtual bool sync() = 0;

	void get_and_reset_stats(uint64_t *const bytes_read, uint64_t *const bytes_written, uint64_t *const n_syncs, uint64_t *const n_trims);

	enum cmpwrite_result_t { CWR_OK, CWR_MISMATCH, CWR_READ_ERROR, CWR_WRITE_ERROR };

	virtual bool write   (const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data) = 0;
	virtual bool trim    (const uint64_t block_nr, const uint32_t n_blocks                           ) = 0;
	virtual bool read    (const uint64_t block_nr, const uint32_t n_blocks,       uint8_t *const data) = 0;
	virtual backend::cmpwrite_result_t cmpwrite(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data_write, const uint8_t *const data_compare) = 0;
};
