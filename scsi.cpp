#include <cassert>
#include <cstdio>
#include <cstring>

#include "scsi.h"


scsi::scsi()
{
}

scsi::~scsi()
{
}

std::tuple<iscsi_pdu_bhs::iscsi_bhs_opcode, uint8_t *, size_t> scsi::send(const uint8_t *const CDB, const size_t size)
{
	assert(size >= 16);

	scsi_opcode opcode = scsi_opcode(CDB[0]);
	printf("SCSI opcode: %02x\n", opcode);

	uint8_t         *reply      = nullptr;
	size_t           reply_size = 0;
	iscsi_pdu_bhs::iscsi_bhs_opcode opcode_hint = iscsi_pdu_bhs::iscsi_bhs_opcode::o_reject;

	if (opcode == 0x00) {  // TEST UNIT READY
		reply_size = 6;
		reply = new uint8_t[reply_size]();
		reply[0] = opcode;
		reply[5] = 0x00;  // CONTROL
		opcode_hint = iscsi_pdu_bhs::iscsi_bhs_opcode::o_scsi_resp;
	}
	else if (opcode == 0x12) {  // INQUIRY
		reply_size = 6 + 36;
		reply = new uint8_t[reply_size]();
		reply[0] = opcode;
		reply[1] = 0;  // no page code
		reply[3] = 0;  // MSB...
		reply[4] = 36;  // ...LSB
		reply[5] = 0x00;  // CONTROL

		uint8_t *data = &reply[6];
		data[0] = 0;  // disk
		data[1] = 0;  // not removable
		data[2] = 4;  // VERSION
		data[3] = 2;  // response data format
		data[4] = 36 - 4;  // additional length
		data[5] = 0;
		data[6] = 0;
		data[7] = 0;
		memcpy(&data[8],  "vanHeusden", 10);
		memcpy(&data[16], "prodinfo", 8);  // TODO
		memcpy(&data[32], "1.0", 3);  // TODO
		opcode_hint = iscsi_pdu_bhs::iscsi_bhs_opcode::o_scsi_data_in;
	}

	return { opcode_hint, reply, reply_size };
}
