#include <cassert>
#include <cstdio>

#include "scsi.h"


scsi::scsi()
{
}

scsi::~scsi()
{
}

std::pair<uint8_t *, size_t> scsi::send(const uint8_t *const CDB, const size_t size)
{
	assert(size >= 16);

	scsi_opcode opcode = scsi_opcode(CDB[0]);
	printf("SCSI opcode: %02x\n", opcode);

	uint8_t *reply = new uint8_t[6]();
	reply[0] = 0x00;  // OPERATION CODE (00h)
	reply[5] = 0x00;  // CONTROL

	return { reply, 6 };
}
