#include "com.h"


class com_client_sockets : public com_client
{
private:
	const int               fd   { -1      };

public:
	com_client_sockets(const int fd, std::atomic_bool *const stop);
	virtual ~com_client_sockets();

	std::string get_local_address() const override;
	std::string get_endpoint_name() const override;

	bool recv(uint8_t *const to, const size_t n)         override;
	bool send(const uint8_t *const from, const size_t n) override;
};

class com_sockets : public com
{
private:
	std::string             listen_ip;
	const int               listen_port;
	int                     listen_fd   { -1 };

public:
	com_sockets(const std::string & listen_ip, const int listen_port, std::atomic_bool *const stop);
	virtual ~com_sockets();

	bool begin() override;

	com_client *accept() override;
};
