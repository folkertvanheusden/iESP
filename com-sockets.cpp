#include <atomic>
#include <errno.h>
#include <cstring>
#ifdef ESP32
#include <Arduino.h>
#elif defined(__MINGW32__)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <poll.h>
#endif
#include <unistd.h>
#if !defined(__MINGW32__)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
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
		DOLOG(logging::ll_error, "com_sockets::begin", get_local_address(), "failed to create socket: %s", strerror(errno));
		return false;
	}

        int reuse_addr = 1;
        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(&reuse_addr), sizeof reuse_addr) == -1) {
		DOLOG(logging::ll_error, "com_sockets::begin", get_local_address(), "failed to set socket to reuse address: %s", strerror(errno));
		return false;
	}

#if !defined(ARDUINO) && !defined(__MINGW32__)
	int q_size = SOMAXCONN;
#ifdef linux 
	if (setsockopt(listen_fd, SOL_TCP, TCP_FASTOPEN, &q_size, sizeof q_size)) {
#else
	if (setsockopt(listen_fd, IPPROTO_TCP, TCP_FASTOPEN, &q_size, sizeof q_size)) {
#endif
		DOLOG(logging::ll_error, "com_sockets::begin", get_local_address(), "failed to set \"TCP fast open\": %s", strerror(errno));
		return false;
	}
#endif

        sockaddr_in server_addr { };
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(listen_port);
#if !defined(__MINGW32__)
	if (inet_aton(listen_ip.c_str(), &reinterpret_cast<sockaddr_in *>(&server_addr)->sin_addr) == 0) {
		DOLOG(logging::ll_error, "com_sockets::begin", get_local_address(), "failed to translate listen address (%s): %s", listen_ip.c_str(), strerror(errno));
		return false;
	}
#else
	if (inet_pton(AF_INET, listen_ip.c_str(), &reinterpret_cast<sockaddr_in *>(&server_addr)->sin_addr) == 0) {
		DOLOG(logging::ll_error, "com_sockets::begin", get_local_address(), "failed to translate listen address (%s): %s", listen_ip.c_str(), strerror(errno));
		return false;
	}
#endif

        if (bind(listen_fd, reinterpret_cast<sockaddr *>(&server_addr), sizeof server_addr) == -1) {
		DOLOG(logging::ll_error, "com_sockets::begin", get_local_address(), "failed to bind socket to %s:%d: %s", listen_ip.c_str(), listen_port, strerror(errno));
		return false;
	}

        if (listen(listen_fd, 4) == -1) {
		DOLOG(logging::ll_error, "com_sockets::begin", get_local_address(), "failed to setup listen queue: %s", strerror(errno));
                return false;
	}

	return true;
}

com_sockets::~com_sockets()
{
	close(listen_fd);
}

com_client *com_sockets::accept()
{
	struct pollfd fds[] { { listen_fd, POLLIN, 0 } };

#if !defined(__MINGW32__)
	for(;;) {
		int rc = poll(fds, 1, 100);
		if (rc == -1) {
			DOLOG(logging::ll_error, "com_sockets::accept", get_local_address(), "poll failed with error %s", strerror(errno));
			return nullptr;
		}

		if (rc >= 1)
			break;

		if (*stop) {
			DOLOG(logging::ll_info, "com_sockets::accept", get_local_address(), "stop flag set");
			return nullptr;
		}
	}
#endif

	int fd = ::accept(listen_fd, nullptr, nullptr);
	if (fd == -1) {
		DOLOG(logging::ll_error, "com_sockets::accept", get_local_address(), "accept failed: %s", strerror(errno));
		return nullptr;
	}

	return new com_client_sockets(fd, stop);
}

com_client_sockets::com_client_sockets(const int fd, std::atomic_bool *const stop): com_client(stop), fd(fd)
{
	int flags = 1;
#if defined(__FreeBSD__) || defined(ESP32) || defined(__MINGW32__)
	if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&flags, sizeof(flags)) == -1)
#else
	if (setsockopt(fd, SOL_TCP, TCP_NODELAY, (void *)&flags, sizeof(flags)) == -1)
#endif
		DOLOG(logging::ll_error, "com_client_sockets", get_endpoint_name(), "cannot disable Nagle algorithm");
}

com_client_sockets::~com_client_sockets()
{
	close(fd);
}

bool com_client_sockets::send(const uint8_t *const from, const size_t n)
{
	auto rc = WRITE(fd, from, n);
	if (rc == -1)
		DOLOG(logging::ll_error, "com_client_sockets::send", get_endpoint_name(), "write failed with error %s", strerror(errno));
	return rc == ssize_t(n);
}

bool com_client_sockets::recv(uint8_t *const to, const size_t n)
{
	pollfd fds[] { { fd, POLLIN, 0 } };
	size_t offset = 0;
	size_t todo   = n;

	while(todo > 0) {
#if defined(__MINGW32__)
		int rc = 1;  // uggly hack
#else
		int rc = poll(fds, 1, 100);
		if (rc == -1) {
			DOLOG(logging::ll_error, "com_client_sockets::recv", get_endpoint_name(), "poll failed with error %s", strerror(errno));
			break;
		}

		if (*stop == true) {
			DOLOG(logging::ll_info, "com_client_sockets::recv", get_endpoint_name(), "abort due external stop");
			break;
		}
#endif

		if (rc >= 1) {
			int n_read = read(fd, &to[offset], todo);
			if (n_read == -1) {
				DOLOG(logging::ll_error, "com_client_sockets::recv", get_endpoint_name(), "read failed with error %s", strerror(errno));
				break;
			}

			if (n_read == 0) {
				DOLOG(logging::ll_info, "com_client_sockets::recv", get_endpoint_name(), "socket closed");
				break;
			}

			offset += n_read;
			todo   -= n_read;
		}
	}

	return todo == 0;
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
                DOLOG(logging::ll_error, "get_endpoint_name", "-", "failed to find name of fd %d", fd);
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

std::string com_client_sockets::get_local_address() const
{
        char host[256];
#ifdef ESP32
        sockaddr_in addr { };
#else
        char         serv[16];
        sockaddr_in6 addr { };
#endif
        socklen_t addr_len = sizeof addr;

        if (getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &addr_len) == -1) {
                DOLOG(logging::ll_error, "get_local_address", get_endpoint_name(), "failed to find local name of fd %d", fd);
		return "?:?";
	}

#ifdef ESP32
	inet_ntop(addr.sin_family, &addr.sin_addr.s_addr, host, sizeof host);
	return host + myformat(":%d", addr.sin_port);
#else
	getnameinfo(reinterpret_cast<sockaddr *>(&addr), addr_len, host, sizeof(host), serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
	return myformat("%s:%s", host, serv);
#endif
}
