#pragma once
#include <cstdint>


class session
{
private:
	uint32_t itt { 0 }, data_sn { 0 };

public:
	session();
	virtual ~session();

	uint32_t get_inc_datasn(const uint32_t itt);
};
