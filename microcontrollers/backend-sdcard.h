#include <mutex>
#include <SdFat.h>
#include <string>

#include "backend.h"


class backend_sdcard : public backend
{
private:
	SdFs      sd;
	FsFile    file;
	uint64_t  card_size   { 0  };
	size_t    sector_size { 0  };
	const int led_read    { -1 };
	const int led_write   { -1 };

	std::mutex serial_access_lock;

	bool reinit(const bool close_first);

public:
	backend_sdcard(const int led_read, const int led_write);
	virtual ~backend_sdcard();

	bool begin() override;

	uint64_t get_size_in_blocks() const override;
	uint64_t get_block_size()     const override;

	bool sync() override;

	bool write(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data) override;
	bool trim (const uint64_t block_nr, const uint32_t n_blocks                           ) override;
	bool read (const uint64_t block_nr, const uint32_t n_blocks,       uint8_t *const data) override;
	backend::cmpwrite_result_t cmpwrite(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data_write, const uint8_t *const data_compare);
};
