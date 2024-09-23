#include <cinttypes>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include "backend-file.h"
#include "log.h"
#include "utils.h"


backend_file::backend_file(const std::string & filename): filename(filename), fd(-1)
{
}

backend_file::~backend_file()
{
	if (fd != -1)
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
	auto rc = lseek(fd, 0, SEEK_END);
	if (rc == -1)
		DOLOG("backend_file::get_size_in_blocks: lseek failed: %s\n", strerror(errno));

	return rc / get_block_size();
}

uint64_t backend_file::get_block_size() const
{
#if defined(ARDUINO)
	return 512;
#else
	return 4096;
#endif
}

bool backend_file::sync()
{
	n_syncs++;
	ts_last_acces = get_micros();
	if (fdatasync(fd) == 0)
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
	auto lock_list = lock_range(block_nr, 1);
	ssize_t rc = pwrite(fd, data, n_bytes, offset);
	unlock_range(lock_list);
	if (rc == -1)
		DOLOG("backend_file::write: ERROR writing; %s\n", strerror(errno));
	ts_last_acces = get_micros();
	bytes_written += n_bytes;
	return rc == ssize_t(n_bytes);
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
	// no locking! write() takes care of that itself!
	// so trim() is not "atomic" at all
	int rc = 0;
	uint8_t *zero = new uint8_t[block_size]();
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
	auto lock_list = lock_range(block_nr, n_blocks);
	ssize_t rc = pread(fd, data, n_bytes, offset);
	unlock_range(lock_list);
	if (rc == -1)
		DOLOG("backend_file::read: ERROR reading; %s\n", strerror(errno));
	else if (rc != ssize_t(n_bytes))
		DOLOG("backend_file::read: short read, requested: %zu, received: %zd\n", n_bytes, rc);
	ts_last_acces = get_micros();
	bytes_read += n_bytes;
	return rc == ssize_t(n_bytes);
}

backend::cmpwrite_result_t backend_file::cmpwrite(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data_write, const uint8_t *const data_compare)
{
	auto lock_list  = lock_range(block_nr, n_blocks);
	auto block_size = get_block_size();

	DOLOG("backend_file::cmpwrite: block %" PRIu64 " (%lu), %d blocks (%zu), block size: %" PRIu64 "\n", block_nr, block_nr * block_size, n_blocks, n_blocks * block_size, block_size);

	cmpwrite_result_t result = cmpwrite_result_t::CWR_OK;
	uint8_t          *buffer = new uint8_t[block_size]();

	// DO
	for(uint32_t i=0; i<n_blocks; i++ ) {
		// read
		off_t   offset = (block_nr + i) * block_size;
		ssize_t rc     = pread(fd, buffer, block_size, offset);
		if (rc != ssize_t(block_size)) {
			if (rc == -1)
				DOLOG("backend_file::cmpwrite: ERROR reading; %s\n", strerror(errno));
			else
				DOLOG("backend_file::cmpwrite: short read, requested: %zu, received: %zd\n", block_size, rc);
			result = cmpwrite_result_t::CWR_READ_ERROR;
			break;
		}
		bytes_read += block_size;

		// compare
		if (memcmp(buffer, &data_compare[i * block_size], block_size) != 0) {
			DOLOG("backend_file::cmpwrite: data mismatch\n");
			result = cmpwrite_result_t::CWR_MISMATCH;
			break;
		}
	}

	if (result == cmpwrite_result_t::CWR_OK) {
		// write
		ssize_t rc = pwrite(fd, data_write, n_blocks * block_size, block_nr * block_size);
		if (rc != ssize_t(n_blocks * block_size)) {
			if (rc == -1)
				DOLOG("backend_file::cmpwrite: ERROR writing; %s\n", strerror(errno));
			else
				DOLOG("backend_file::cmpwrite: short write, sent: %zu, written: %zd\n", n_blocks * block_size, rc);
			result = cmpwrite_result_t::CWR_WRITE_ERROR;
		}
		else {
			bytes_written += block_size;

			ts_last_acces = get_micros();
		}
	}

	delete [] buffer;

	unlock_range(lock_list);

	return result;
}
