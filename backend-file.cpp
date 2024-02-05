#include <cinttypes>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "backend-file.h"
#include "log.h"


backend_file::backend_file(const std::string & filename)
{
	fd = open(filename.c_str(), O_RDWR);
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
	return 512;
}

bool backend_file::write(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data)
{
	auto   block_size = get_block_size();
	off_t  offset     = block_nr * block_size;
	size_t n_bytes    = n_blocks * block_size;
	DOLOG("backend_file::write: block %" PRIu64 " (%lu), %d blocks, block size: %" PRIu64 "\n", block_nr, offset, n_blocks, block_size);
	ssize_t rc = pwrite(fd, data, n_bytes, offset);
	if (rc == -1)
		DOLOG("backend_file::write: ERROR writing; %s\n", strerror(errno));
	return rc == n_bytes;
}

bool backend_file::read(const uint64_t block_nr, const uint32_t n_blocks, uint8_t *const data)
{
	auto   block_size = get_block_size();
	off_t  offset     = block_nr * block_size;
	size_t n_bytes    = n_blocks * block_size;
	DOLOG("backend_file::read: block %" PRIu64 " (%lu), %d blocks (%zu), block size: %" PRIu64 "\n", block_nr, offset, n_blocks, n_bytes, block_size);
	ssize_t rc = pread(fd, data, n_bytes, offset);
	if (rc == -1)
		DOLOG("backend_file::read: ERROR reading; %s\n", strerror(errno));
	else if (rc != n_bytes)
		DOLOG("backend_file::read: short read, requested: %zu, received: %zd\n", n_bytes, rc);
	return rc == n_bytes;
}
