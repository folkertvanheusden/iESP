#include <cinttypes>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include "backend-file.h"
#include "log.h"
#include "utils.h"


backend_file::backend_file(const std::string & filename): backend(filename), filename(filename), fd(-1)
{
}

backend_file::~backend_file()
{
	if (fd != -1)
		close(fd);
}

bool backend_file::begin()
{
#if defined(__MINGW32__)
	fd = open(filename.c_str(), O_RDWR | O_BINARY);
#else
	fd = open(filename.c_str(), O_RDWR);
#endif
	if (fd == -1) {
		DOLOG(logging::ll_error, "backend_file", identifier, "cannot access file %s: %s", filename.c_str(), strerror(errno));
		return false;
	}

	return true;
}

uint64_t backend_file::get_size_in_blocks() const
{
	auto rc = lseek(fd, 0, SEEK_END);
	if (rc == -1)
		DOLOG(logging::ll_error, "backend_file::get_size_in_blocks", identifier, "lseek failed: %s", strerror(errno));

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
#if defined(__MINGW32__)
	if (_commit(fd) == 0)
		return true;
#elif defined(__APPLE__)
	if (fsync(fd) == 0)
		return true;
#else
	if (fdatasync(fd) == 0)
		return true;
#endif

	DOLOG(logging::ll_error, "backend_file::sync", identifier, "failed: %s", strerror(errno));

	return false;
}

bool backend_file::write(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data)
{
	auto   block_size = get_block_size();
	off_t  offset     = block_nr * block_size;
	size_t n_bytes    = n_blocks * block_size;
	DOLOG(logging::ll_debug, "backend_file::write", identifier, "block %" PRIu64 " (%lu), %d blocks, block size: %" PRIu64, block_nr, offset, n_blocks, block_size);
#if defined(__MINGW32__)
	std::unique_lock<std::mutex> lck(io_lock);
	int rc = lseek(fd, offset, SEEK_SET);
	if (rc != -1)
		rc = ::write(fd, data, n_bytes);
#else
	auto lock_list = lock_range(block_nr, 1);
	ssize_t rc = pwrite(fd, data, n_bytes, offset);
	unlock_range(lock_list);
#endif
	if (rc == -1)
		DOLOG(logging::ll_error, "backend_file::write", identifier, "ERROR writing: %s", strerror(errno));
	ts_last_acces = get_micros();
	bytes_written += n_bytes;
	return rc == ssize_t(n_bytes);
}

bool backend_file::trim(const uint64_t block_nr, const uint32_t n_blocks)
{
	auto   block_size = get_block_size();
	off_t  offset     = block_nr * block_size;
	size_t n_bytes    = n_blocks * block_size;
	DOLOG(logging::ll_debug, "backend_file::trim", identifier, "block %" PRIu64 " (%lu), %d blocks, block size: %" PRIu64, block_nr, offset, n_blocks, block_size);
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
		DOLOG(logging::ll_error, "backend_file::trim", identifier, "unmapping: %s", strerror(errno));
	n_trims++;
	ts_last_acces = get_micros();
	return rc == 0;
}

bool backend_file::read(const uint64_t block_nr, const uint32_t n_blocks, uint8_t *const data)
{
	auto     block_size = get_block_size();
	off_t    offset     = block_nr * block_size;
	size_t   n_bytes    = n_blocks * block_size;
	DOLOG(logging::ll_debug, "backend_file::read", identifier, "block %" PRIu64 " (%lu), %d blocks (%zu), block size: %" PRIu64, block_nr, offset, n_blocks, n_bytes, block_size);
#if defined(__MINGW32__)
	std::unique_lock<std::mutex> lck(io_lock);
	ssize_t rc = 0;
	if (lseek(fd, offset, SEEK_SET) != off_t(-1))
		rc = READ(fd, data, n_bytes);
	else
		DOLOG(logging::ll_error, "backend_file::read", identifier, "lseek failed: %s", strerror(errno));
#else
	auto lock_list = lock_range(block_nr, n_blocks);
	ssize_t rc = pread(fd, data, n_bytes, offset);
	unlock_range(lock_list);
#endif
	if (rc == -1)
		DOLOG(logging::ll_error, "backend_file::read", identifier, "error reading: %s", strerror(errno));
	else if (rc != ssize_t(n_bytes))
		DOLOG(logging::ll_error, "backend_file::read", identifier, "short read, requested: %zu, received: %zd", n_bytes, rc);
	ts_last_acces = get_micros();
	bytes_read += n_bytes;
	return rc == ssize_t(n_bytes);
}

backend::cmpwrite_result_t backend_file::cmpwrite(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data_write, const uint8_t *const data_compare)
{
	auto block_size = get_block_size();

	DOLOG(logging::ll_debug, "backend_file::cmpwrite", identifier, "block %" PRIu64 " (%lu), %d blocks (%zu), block size: %" PRIu64, block_nr, block_nr * block_size, n_blocks, n_blocks * block_size, block_size);

	cmpwrite_result_t result = cmpwrite_result_t::CWR_OK;
	uint8_t          *buffer = new uint8_t[block_size]();

#if defined(__MINGW32__)
	std::unique_lock<std::mutex> lck(io_lock);
#else
	auto lock_list  = lock_range(block_nr, n_blocks);
#endif

	// DO
	for(uint32_t i=0; i<n_blocks; i++ ) {
		// read
		off_t   offset = (block_nr + i) * block_size;
#if defined(__MINGW32__)
		int rc = lseek(fd, offset, SEEK_SET);
		if (rc != -1)
			rc = ::read(fd, buffer, block_size);
#else
		ssize_t rc     = pread(fd, buffer, block_size, offset);
#endif
		if (rc != ssize_t(block_size)) {
			if (rc == -1)
				DOLOG(logging::ll_error, "backend_file::cmpwrite", identifier, "error reading: %s", strerror(errno));
			else
				DOLOG(logging::ll_error, "backend_file::cmpwrite", identifier, "short read, requested: %zu, received: %zd", block_size, rc);
			result = cmpwrite_result_t::CWR_READ_ERROR;
			break;
		}
		bytes_read += block_size;

		// compare
		if (memcmp(buffer, &data_compare[i * block_size], block_size) != 0) {
			DOLOG(logging::ll_warning, "backend_file::cmpwrite", identifier, "data does not match");  // is this really a warning?
			result = cmpwrite_result_t::CWR_MISMATCH;
			break;
		}
	}

	if (result == cmpwrite_result_t::CWR_OK) {
		// write
#if defined(__MINGW32__)
		int rc = lseek(fd, block_nr * block_size, SEEK_SET);
		if (rc != -1)
			rc = ::write(fd, data_write, n_blocks * block_size);
#else
		ssize_t rc = pwrite(fd, data_write, n_blocks * block_size, block_nr * block_size);
#endif
		if (rc != ssize_t(n_blocks * block_size)) {
			if (rc == -1)
				DOLOG(logging::ll_error, "backend_file::cmpwrite", identifier, "error writing: %s", strerror(errno));
			else
				DOLOG(logging::ll_error, "backend_file::cmpwrite", identifier, "short write, sent: %zu, written: %zd", n_blocks * block_size, rc);
			result = cmpwrite_result_t::CWR_WRITE_ERROR;
		}
		else {
			bytes_written += block_size;

			ts_last_acces = get_micros();
		}
	}

	delete [] buffer;

#if !defined(__MINGW32__)
	unlock_range(lock_list);
#endif

	return result;
}
