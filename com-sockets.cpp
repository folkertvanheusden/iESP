#include <atomic>
#include <errno.h>
#include <cstring>
#include <netdb.h>
#ifndef ESP32
#include <poll.h>
#endif
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#ifdef ESP32
#ifndef SOL_TCP
#define SOL_TCP 6
#endif
#endif

#include "com-sockets.h"
#include "log.h"
#include "utils.h"


com_sockets::com_sockets(const std::string & listen_ip, const int listen_port, std::atomic_bool *const stop):
	com(stop),
	listen_ip(listen_ip),
	listen_port(listen_port)
{
}

bool com_sockets::begin()
{
	// setup listening socket for viewers
	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd == -1) {
		DOLOG("com_sockets::begin: failed to create socket: %s\n", strerror(errno));
		return false;
	}

        int reuse_addr = 1;
        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(&reuse_addr), sizeof reuse_addr) == -1) {
		DOLOG("com_sockets::begin: failed to set socket to reuse address: %s\n", strerror(errno));
		return false;
	}

#ifdef linux
	int q_size = SOMAXCONN;
	if (setsockopt(listen_fd, SOL_TCP, TCP_FASTOPEN, &q_size, sizeof q_size)) {
		DOLOG("com_sockets::begin: failed to set \"TCP fast open\": %s\n", strerror(errno));
		return false;
	}
#endif

        sockaddr_in server_addr { };
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(listen_port);
	if (inet_aton(listen_ip.c_str(), &reinterpret_cast<sockaddr_in *>(&server_addr)->sin_addr) == 0) {
		DOLOG("com_sockets::begin: failed to translate listen address (%s): %s\n", listen_ip.c_str(), strerror(errno));
		return false;
	}

        if (bind(listen_fd, reinterpret_cast<sockaddr *>(&server_addr), sizeof server_addr) == -1) {
		DOLOG("com_sockets::begin: failed to bind socket to %s:%d: %s\n", listen_ip.c_str(), listen_port, strerror(errno));
		return false;
	}

        if (listen(listen_fd, 4) == -1) {
		DOLOG("com_sockets::begin: failed to setup listen queue: %s\n", strerror(errno));
                return false;
	}

	return true;
}

com_sockets::~com_sockets()
{
	close(listen_fd);
}

std::string com_sockets::get_local_address()
{
	return listen_ip + myformat(":%d", listen_port);
}

com_client *com_sockets::accept()
{
	struct pollfd fds[] { { listen_fd, POLLIN, 0 } };

#ifndef ESP32
	for(;;) {
		int rc = poll(fds, 1, 100);
		if (rc == -1) {
			DOLOG("server::handler: poll failed with error %s\n", strerror(errno));
			return nullptr;
		}

		if (rc >= 1)
			break;

		if (*stop) {
			DOLOG("server::handler: stop flag set\n");
			return nullptr;
		}
	}
#endif

	int fd = ::accept(listen_fd, nullptr, nullptr);
	if (fd == -1) {
		DOLOG("com_sockets::accept: accept failed: %s\n", strerror(errno));
		return nullptr;
	}

	return new com_client_sockets(fd, stop);
}

com_client_sockets::com_client_sockets(const int fd, std::atomic_bool *const stop): com_client(stop), fd(fd)
{
	int flags = 1;
#if defined(__FreeBSD__)
	if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void *)&flags, sizeof(flags)) == -1)
#else
	if (setsockopt(fd, SOL_TCP, TCP_NODELAY, (void *)&flags, sizeof(flags)) == -1)
#endif
		DOLOG("server::handler: cannot disable Nagle algorithm\n");
}

com_client_sockets::~com_client_sockets()
{
	close(fd);
}

bool com_client_sockets::send(const uint8_t *const from, const size_t n)
{
	auto rc = WRITE(fd, from, n);
	if (rc == -1) {
#ifdef ESP32
		printf("com_client_sockets::send: write failed with error %s\r\n", strerror(errno));
#else
		DOLOG("com_client_sockets::send: write failed with error %s\n", strerror(errno));
#endif
	}
	return rc == n;
}

bool com_client_sockets::recv(uint8_t *const to, const size_t n)
{
#ifndef ESP32
	pollfd fds[] { { fd, POLLIN, 0 } };

	for(;;) {
		int rc = poll(fds, 1, 100);
		if (rc == -1) {
			DOLOG("com_client_sockets::recv: poll failed with error %s\n", strerror(errno));
			return false;
		}

		if (*stop == true) {
			DOLOG("com_client_sockets::recv: abort due external stop\n");
			return false;
		}

		if (rc >= 1)
			break;
	}
#endif

	// ideally the poll-loop should include the read (TODO)
	auto rc = READ(fd, to, n);

	if (rc == -1) {
#ifdef ESP32
		printf("com_client_sockets::recv: read failed with error %s\r\n", strerror(errno));
#else
		DOLOG("com_client_sockets::recv: read failed with error %s\n", strerror(errno));
#endif
	}

	return rc == n;
}

std::string com_client_sockets::get_endpoint_name() const
{
        char host[256];
#ifdef ESP32
        sockaddr_in addr { };
#else
        char         serv[16];
        sockaddr_in6 addr { };
#endif
        socklen_t addr_len = sizeof addr;

        if (getpeername(fd, reinterpret_cast<sockaddr *>(&addr), &addr_len) == -1) {
                DOLOG("get_endpoint_name: failed to find name of fd %d\n", fd);
		return "?:?";
	}

#ifdef ESP32
	inet_ntop(addr.sin_family, &addr.sin_addr.s_addr, host, sizeof host);
	return host + myformat(":%d", addr.sin_port);
#else
	getnameinfo(reinterpret_cast<sockaddr *>(&addr), addr_len, host, sizeof(host), serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
	return myformat("[%s]:%s", host, serv);
#endif
}
