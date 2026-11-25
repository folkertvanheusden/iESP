#pragma once
#include <functional>
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
	const std::function<void()> idle_poll;
	// The microcontroller serves one session at a time (no threads), so a stuck or vanished
	// initiator must never hold it: end the session after IDLE_TIMEOUT_MS without any data
	// (initiators send NOP-Outs every few seconds) or MAX_SESSION_MS in total.
	static constexpr unsigned long IDLE_TIMEOUT_MS { 20000 };
	static constexpr unsigned long MAX_SESSION_MS  { 60000 };
	const unsigned long t_start;
	bool timed_out(const unsigned long t_last_data) const;
#if !defined(TEENSY4_1)
	// While this session is active, any other initiator that connects is closed at once (it gets
	// a quick failure and retries) instead of hanging in login until this session ends.
	WiFiServer *const srv;
	void reject_pending() const;
#endif

public:
#if defined(TEENSY4_1)
	com_client_arduino(qn::EthernetClient & wc, std::function<void()> idle_poll);
#else
	com_client_arduino(WiFiClient & wc, std::function<void()> idle_poll, WiFiServer *const srv);
#endif
	virtual ~com_client_arduino();

	std::string get_local_address() const override;
	std::string get_endpoint_name() const override;

	bool recv(uint8_t *const to, const size_t n)         override;
	bool send(const uint8_t *const from, const size_t n) override;
};

class com_arduino : public com
{
private:
#if !defined(TEENSY4_1)
	static constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS { 15000 };  // begin() gives up after this
	static constexpr unsigned long WIFI_LOST_REBOOT_MS     { 10000 };  // reboot if WiFi stays down while idle
#endif
	const int   port   { 3260    };
	const std::function<void()> idle_poll;
#if defined(TEENSY4_1)
	qn::EthernetServer *server { nullptr };
#else
	WiFiServer *server { nullptr };
#endif

public:
	com_arduino(const int port, std::function<void()> idle_poll);
	virtual ~com_arduino();

	bool begin() override;

	std::string get_local_address() const override;

	com_client *accept() override;
};
