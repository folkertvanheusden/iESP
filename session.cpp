#include "session.h"


session::session()
{
}

session::~session()
{
}

uint32_t session::get_inc_datasn(const uint32_t itt)
{
	if (itt == this->itt)
		return data_sn++;

	this->itt = itt;
	return data_sn++;
}
