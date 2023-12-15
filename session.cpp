#include <cstring>

#include "session.h"


session::session(const uint8_t ISID[6])
{
	memcpy(this->ISID, ISID, 6);
}

session::~session()
{
}
