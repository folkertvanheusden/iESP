#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "backend-file.h"
#include "log.h"


backend_file::backend_file(const std::string & filename)
{
	fd = open(filename.c_str(), O_RDONLY);
	if (fd == -1)
		DOLOG("backend_file:: cannot access file %s\n", filename.c_str());
}

backend_file::~backend_file()
{
	close(fd);
}

uint64_t backend_file::get_size_in_blocks() const
{
	struct stat st { };
	if (fstat(fd, &st) == -1)
		DOLOG("backend_file::get_size_in_blocks: fstat failed\n");

	return st.st_size / get_block_size();
}

uint64_t backend_file::get_block_size() const
{
	return 4096;
}

bool backend_file::write(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data)
{
	auto   block_size = get_block_size();
	size_t n_bytes    = n_blocks * block_size;
	return pwrite(fd, data, n_bytes, block_nr * block_size) == n_bytes;
}

bool backend_file::read(const uint64_t block_nr, const uint32_t n_blocks, uint8_t *const data)
{
	auto   block_size = get_block_size();
	size_t n_bytes    = n_blocks * block_size;
	return pread(fd, data, n_bytes, block_nr * block_size) == n_bytes;
}
