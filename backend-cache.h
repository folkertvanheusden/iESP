#include <mutex>
#include <string>

#include "backend.h"
#include "cache.h"


class backend_cache : public backend
{
private:
	backend    *const parent      {};
	const size_t      max_n_bytes {};
	std::mutex        cache_lock;
	cache            *c           {};

	void     invalidate_range(const uint64_t block_nr, const uint32_t n_blocks);
	uint8_t *duplicate_block (const uint8_t *const p, const size_t s);

public:
	backend_cache(backend *const parent, const size_t max_n_bytes);
	virtual ~backend_cache();

	bool begin() override;

	std::string get_serial()         const override;
	uint64_t    get_size_in_blocks() const override;
	uint64_t    get_block_size()     const override;

	bool sync() override;

	bool prefetch(const uint64_t block_nr, const uint32_t n_blocks                           ) override;
	bool write   (const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data) override;
	bool trim    (const uint64_t block_nr, const uint32_t n_blocks                           ) override;
	bool read    (const uint64_t block_nr, const uint32_t n_blocks,       uint8_t *const data) override;
	backend::cmpwrite_result_t cmpwrite(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data_write, const uint8_t *const data_compare) override;
};
