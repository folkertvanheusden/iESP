#include <cinttypes>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "backend-file.h"
#include "log.h"
#include "utils.h"


backend_file::backend_file(const std::string & filename): filename(filename), fd(-1)
{
}

backend_file::~backend_file()
{
	close(fd);
}

bool backend_file::begin()
{
	fd = open(filename.c_str(), O_RDWR);
	if (fd == -1) {
		DOLOG("backend_file:: cannot access file %s: %s\n", filename.c_str(), strerror(errno));
		return false;
	}

	return true;
}

uint64_t backend_file::get_size_in_blocks() const
{
	struct stat st { };
	if (fstat(fd, &st) == -1)
		DOLOG("backend_file::get_size_in_blocks: fstat failed: %s\n", strerror(errno));

	return st.st_size / get_block_size();
}

uint64_t backend_file::get_block_size() const
{
	return 512;
}

bool backend_file::sync()
{
	ts_last_acces = get_micros();
	if (fsync(fd) == 0)
		return true;

	DOLOG("backend_file::sync: failed: %s\n", strerror(errno));

	return false;
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
	ts_last_acces = get_micros();
	return rc == n_bytes;
}

bool backend_file::trim(const uint64_t block_nr, const uint32_t n_blocks)
{
	auto   block_size = get_block_size();
	off_t  offset     = block_nr * block_size;
	size_t n_bytes    = n_blocks * block_size;
	DOLOG("backend_file::trim: block %" PRIu64 " (%lu), %d blocks, block size: %" PRIu64 "\n", block_nr, offset, n_blocks, block_size);
#ifdef linux
	int rc = fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, offset, n_bytes);
#else
	int rc = 0;
	uint8_t *zero = new uint8_t[512]();
	for(uint32_t i=0; i<n_blocks; i++) {
		if (write(block_nr + i, 1, zero) == false) {
			rc = -1;
			break;
		}
	}
#endif
	if (rc == -1)
		DOLOG("backend_file::trim: ERROR unmaping; %s\n", strerror(errno));
	n_trims++;
	ts_last_acces = get_micros();
	return rc == 0;
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
	ts_last_acces = get_micros();
	return rc == n_bytes;
}
