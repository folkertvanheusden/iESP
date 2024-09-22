#pragma once
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <string>


class com_client
{
protected:
	std::atomic_bool *const stop { nullptr };

public:
	com_client(std::atomic_bool *const stop);
	virtual ~com_client();

	virtual std::string get_local_address() const = 0;
	virtual std::string get_endpoint_name() const = 0;

	virtual bool recv(uint8_t *const to, const size_t n) = 0;
	virtual bool send(const uint8_t *const from, const size_t n) = 0;
};

class com
{
protected:
	std::atomic_bool *const stop { nullptr };

public:
	com(std::atomic_bool *const stop);
	virtual ~com();

	virtual bool begin() = 0;

	virtual com_client *accept() = 0;
};
