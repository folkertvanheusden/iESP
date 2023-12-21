#include <cassert>
#include <cstdio>
#include <cstring>

#include "log.h"
#include "scsi.h"


scsi::scsi()
{
}

scsi::~scsi()
{
}

std::optional<scsi_response> scsi::send(const uint8_t *const CDB, const size_t size)
{
	assert(size >= 16);

	scsi_opcode opcode = scsi_opcode(CDB[0]);
	printf("SCSI opcode: %02x\n", opcode);

	scsi_response response { };

	if (opcode == 0x00) {  // TEST UNIT READY
	}
	else if (opcode == 0x12) {  // INQUIRY
		response.data.second = 36;
		response.data.first = new uint8_t[response.data.second]();
		response.data.first[0] = 0;  // disk
		response.data.first[1] = 0;  // not removable
		response.data.first[2] = 4;  // VERSION
		response.data.first[3] = 2;  // response data format
		response.data.first[4] = response.data.second - 4;  // additional length
		response.data.first[5] = 0;
		response.data.first[6] = 0;
		response.data.first[7] = 0;
		memcpy(&response.data.first[8],  "vnHeusdn", 8);
		memcpy(&response.data.first[16], "iESP", 4);  // TODO
		memcpy(&response.data.first[32], "1.0", 3);  // TODO
	}
	else if (opcode == 0x5E) {  //  PERSISTENT RESERVE IN
		printf("service action: %d\n", CDB[1] & 0x1f);
		return { };  // TODO
	}
	else {
		DOLOG("scsi::send: opcode %02x not implemented\n", opcode);
		return { };
	}

	return response;
}
