#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "backend-sdcard.h"
#include "log.h"


backend_sdcard::backend_sdcard()
{
}

backend_sdcard::~backend_sdcard()
{
}

uint64_t backend_sdcard::get_size_in_blocks() const
{
	return 16;
}

uint64_t backend_sdcard::get_block_size() const
{
	return 512;
}

bool backend_sdcard::write(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data)
{
	return true;
}

bool backend_sdcard::read(const uint64_t block_nr, const uint32_t n_blocks, uint8_t *const data)
{
	return true;
}
