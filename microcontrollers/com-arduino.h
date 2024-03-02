#pragma once
#if defined(TEENSY4_1)
#include <QNEthernet.h>
namespace qn = qindesign::network;
#endif

#include "com.h"


class com_client_arduino : public com_client
{
private:
#if defined(TEENSY4_1)
	mutable qn::EthernetClient wc;
#else
	mutable WiFiClient wc;
#endif

public:
#if defined(TEENSY4_1)
	com_client_arduino(qn::EthernetClient & wc);
#else
	com_client_arduino(WiFiClient & wc);
#endif
	virtual ~com_client_arduino();

	std::string get_endpoint_name() const override;

	bool recv(uint8_t *const to, const size_t n)         override;
	bool send(const uint8_t *const from, const size_t n) override;
};

class com_arduino : public com
{
private:
	const int   port   { 3260    };
#if defined(TEENSY4_1)
	qn::EthernetServer *server { nullptr };
#else
	WiFiServer *server { nullptr };
#endif

public:
	com_arduino(const int port);
	virtual ~com_arduino();

	bool begin() override;

	std::string get_local_address() override;

	com_client *accept() override;
};
