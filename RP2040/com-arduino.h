#include "com.h"


class com_client_arduino : public com_client
{
private:
	mutable WiFiClient wc;

public:
	com_client_arduino(WiFiClient & wc);
	virtual ~com_client_arduino();

	std::string get_endpoint_name() const override;

	bool recv(uint8_t *const to, const size_t n)         override;
	bool send(const uint8_t *const from, const size_t n) override;
};

class com_arduino : public com
{
private:
	const int   port   { 3260    };
	WiFiServer *server { nullptr };

public:
	com_arduino(const int port);
	virtual ~com_arduino();

	bool begin() override;

	std::string get_local_address() override;

	com_client *accept() override;
};
