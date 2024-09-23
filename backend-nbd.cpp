#include <cassert>
#include <cinttypes>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "backend-nbd.h"
#include "log.h"
#include "utils.h"


#define NBD_CMD_READ              0
#define NBD_CMD_WRITE             1
#define NBD_CMD_FLUSH             3
#define NBD_CMD_TRIM              4

backend_nbd::backend_nbd(const std::string & host, const int port):
	host(host), port(port),
	fd(-1)
{
}

backend_nbd::~backend_nbd()
{
	if (fd != -1)
		close(fd);
}

bool backend_nbd::begin()
{
	return connect(false);
}

bool backend_nbd::connect(const bool retry)
{
	if (fd != -1)
		return true;

	do {
		// LOOP until connected, logging message, exponential backoff?
		addrinfo *res     = nullptr;

		addrinfo hints { };
		hints.ai_family   = AF_INET;
		hints.ai_socktype = SOCK_STREAM;

		char port_str[8] { 0 };
		snprintf(port_str, sizeof port_str, "%u", port);

		int rc = getaddrinfo(host.c_str(), port_str, &hints, &res);
		if (rc != 0) {
			DOLOG("Cannot resolve \"%s\"", host.c_str());
			sleep(1);
			continue;
		}

		for(addrinfo *p = res; p != NULL; p = p->ai_next) {
			if ((fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
				DOLOG("Failed to create socket");
				continue;
			}

			if (::connect(fd, p->ai_addr, p->ai_addrlen) == -1) {
				DOLOG("Failed to connect");
				close(fd);
				fd = -1;
				continue;
			}

			break;
		}

		freeaddrinfo(res);

		struct __attribute__ ((packed)) {
			uint8_t  magic1[8];
			uint8_t  magic2[8];
			uint64_t size;
			uint32_t flags;
			uint8_t  padding[124];
		} nbd_hello { };

		if (fd != -1) {
			if (READ(fd, reinterpret_cast<uint8_t *>(&nbd_hello), sizeof nbd_hello) != sizeof nbd_hello) {
				DOLOG("NBD_HELLO receive failed");
				close(fd);
				fd = -1;
			}

			dev_size = NTOHLL(nbd_hello.size);
		}

		if (fd != -1 && memcmp(nbd_hello.magic1, "NBDMAGIC", 8) != 0) {
			DOLOG("NBD_HELLO magic failed");
			close(fd);
			fd = -1;
		}

		if (fd != -1) {
			int flags = 1;
			if (setsockopt(fd, SOL_TCP, TCP_NODELAY, (void *)&flags, sizeof(flags)) == -1)
				DOLOG("TCP_NODELAY failed");
		}
	}
	while(fd == -1 && retry);

	DOLOG("Connected to NBD server");

	return fd != -1;
}

uint64_t backend_nbd::get_size_in_blocks() const
{
	return dev_size / get_block_size();
}

uint64_t backend_nbd::get_block_size() const
{
	return 4096;
}

bool backend_nbd::invoke_nbd(const uint32_t command, const uint64_t offset, const uint32_t n_bytes, uint8_t *const data)
{
	do {
		if (!connect(true)) {
			DOLOG("backend_nbd::invoke_nbd: (re-)connect");
			sleep(1);
			continue;
		}

		struct __attribute__ ((packed)) {
			uint32_t magic;
			uint32_t type;
			uint64_t handle;
			uint64_t offset;
			uint32_t length;
		} nbd_request { };

		nbd_request.magic  = ntohl(0x25609513);
		nbd_request.type   = htonl(command);
		nbd_request.offset = HTONLL(offset);
		nbd_request.length = htonl(n_bytes);

		if (WRITE(fd, reinterpret_cast<const uint8_t *>(&nbd_request), sizeof nbd_request) != sizeof nbd_request) {
			DOLOG("backend_nbd::invoke_nbd: problem sending request");
			close(fd);
			fd = -1;
			sleep(1);
			continue;
		}

		if (command == NBD_CMD_WRITE) {
			if (WRITE(fd, reinterpret_cast<const uint8_t *>(data), n_bytes) != ssize_t(n_bytes)) {
				DOLOG("backend_nbd::invoke_nbd: problem sending payload");
				close(fd);
				fd = -1;
				sleep(1);
				continue;
			}
		}

		struct __attribute__ ((packed)) {
			uint32_t magic;
			uint32_t error;
			uint64_t handle;
		} nbd_reply;

		if (READ(fd, reinterpret_cast<uint8_t *>(&nbd_reply), sizeof nbd_reply) != sizeof nbd_reply) {
			DOLOG("backend_nbd::invoke_nbd: problem receiving reply header");
			close(fd);
			fd = -1;
			sleep(1);
			continue;
		}

		if (ntohl(nbd_reply.magic) != 0x67446698) {
			DOLOG("backend_nbd::invoke_nbd: bad reply header %08x", nbd_reply.magic);
			close(fd);
			fd = -1;
			sleep(1);
			continue;
		}

		int error = ntohl(nbd_reply.error);
		if (error) {
			DOLOG("backend_nbd::invoke_nbd: NBD server indicated error: %d", error);
			return false;
		}

		if (command == NBD_CMD_READ) {
			if (READ(fd, data, n_bytes) != ssize_t(n_bytes)) {
				DOLOG("backend_nbd::invoke_nbd: problem receiving payload");
				close(fd);
				fd = -1;
				sleep(1);
				continue;
			}
		}
	}
	while(fd == -1);

	return fd != -1;
}

bool backend_nbd::sync()
{
	n_syncs++;
	ts_last_acces = get_micros();

	return invoke_nbd(NBD_CMD_FLUSH, 0, 0, nullptr);
}

bool backend_nbd::write(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data)
{
	auto   block_size = get_block_size();
	off_t  offset     = block_nr * block_size;
	size_t n_bytes    = n_blocks * block_size;
	DOLOG("backend_nbd::write: block %" PRIu64 " (%lu), %d blocks, block size: %" PRIu64 "\n", block_nr, offset, n_blocks, block_size);
	auto   lock_list  = lock_range(block_nr, n_blocks);

	bool rc = invoke_nbd(NBD_CMD_WRITE, offset, n_bytes, const_cast<uint8_t *>(data));

	unlock_range(lock_list);

	ts_last_acces = get_micros();
	bytes_written += n_bytes;

	return rc;
}

bool backend_nbd::trim(const uint64_t block_nr, const uint32_t n_blocks)
{
	auto   block_size = get_block_size();
	off_t  offset     = block_nr * block_size;
	size_t n_bytes    = n_blocks * block_size;
	DOLOG("backend_nbd::trim: block %" PRIu64 " (%lu), %d blocks, block size: %" PRIu64 "\n", block_nr, offset, n_blocks, block_size);
	auto   lock_list  = lock_range(block_nr, n_blocks);

	bool rc = invoke_nbd(NBD_CMD_TRIM, offset, n_bytes, nullptr);

	unlock_range(lock_list);

	ts_last_acces = get_micros();
	bytes_written += n_bytes;

	return rc;
}

bool backend_nbd::read(const uint64_t block_nr, const uint32_t n_blocks, uint8_t *const data)
{
	auto   block_size = get_block_size();
	off_t  offset_in  = block_nr * block_size;
	off_t  offset     = offset_in;
	size_t n_bytes    = n_blocks * block_size;
	auto   lock_list  = lock_range(block_nr, n_blocks);

	bool rc = invoke_nbd(NBD_CMD_READ, offset, n_bytes, data);

	unlock_range(lock_list);

	ts_last_acces = get_micros();
	bytes_read += n_bytes;

	return rc;
}

backend::cmpwrite_result_t backend_nbd::cmpwrite(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data_write, const uint8_t *const data_compare)
{
	assert(0);
	// TODO
}
