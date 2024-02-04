#include <SdFat.h>
#include <string>

#include "backend.h"


class backend_sdcard : public backend
{
private:
	SdFs      sd;
	FsFile    file;
	uint64_t  card_size   { 0 };
	size_t    sector_size { 0 };

	void init_file();

public:
	backend_sdcard();
	virtual ~backend_sdcard();

	uint64_t get_size_in_blocks() const override;
	uint64_t get_block_size()     const override;

	bool write(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data) override;
	bool read (const uint64_t block_nr, const uint32_t n_blocks,       uint8_t *const data) override;
};
