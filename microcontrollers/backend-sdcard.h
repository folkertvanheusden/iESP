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
	const int led_read    { -1 };
	const int led_write   { -1 };
	const int pin_SD_MISO { -1 };
	const int pin_SD_MOSI { -1 };
	const int pin_SD_SCLK { -1 };
	const int pin_SD_CS   { -1 };

#if defined(RP2040W)
	mutex_t    serial_access_lock;
#else
	std::mutex serial_access_lock;
#endif

	bool reinit(const bool close_first);

	// wait until it is no longer busy with something and available for writes
	void wait_for_card();

public:
	backend_sdcard(const int led_read, const int led_write, const int pin_SD_MISO, const int pin_SD_MOSI, const int pin_SD_SCLK, const int pin_SD_CS);
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
