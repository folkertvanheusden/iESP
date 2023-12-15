#include <cstring>

#include "session.h"


session::session(const uint8_t ISID[6], const uint16_t CID, const uint32_t CmdSN) :
	CID(CID),
	CmdSN(CmdSN)
{
	memcpy(this->ISID, ISID, 6);
}

session::~session()
{
}
