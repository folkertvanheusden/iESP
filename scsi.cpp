#include <cassert>
#include <cstdio>
#include <cstring>

#include "log.h"
#include "scsi.h"
#include "utils.h"


scsi::scsi()
{
}

scsi::~scsi()
{
}

std::optional<scsi_response> scsi::send(const uint8_t *const CDB, const size_t size, std::optional<std::pair<uint8_t *, size_t> > & data)
{
	assert(size >= 16);

	scsi_opcode opcode = scsi_opcode(CDB[0]);
	DOLOG("SCSI opcode: %02xh, CDB size: %zu\n", opcode, size);
#ifdef linux
	DOLOG("CDB contents: %s\n", to_hex(CDB, size).c_str());
#endif

	scsi_response   response { };
	response.type = ir_as_is;

	if (opcode == o_test_unit_ready) {
	}
	else if (opcode == o_inquiry) {  // 0x12
		DOLOG("scsi::send: INQUIRY\n");
		if (CDB[1] & 1) {
			DOLOG(" INQUIRY: EVPD\n");
			DOLOG(" INQUIRY: PageCode: %02xh\n", CDB[2]);
		}
		if (CDB[1] & 2)
			DOLOG(" INQUIRY: CmdDt\n");
		DOLOG(" INQUIRY: AllocationLength: %d\n", (CDB[3] << 8) | CDB[4]);
		DOLOG(" INQUIRY: ControlByte: %02xh\n", CDB[5]);
		if ((CDB[1] & 1) == 0) {
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
		else {  // PageCode not supported
			response.sense_data = {
				0x72,  // current errors
				0x05,  // key: illegal request
				0x24,  // ASC: invalid field in cdb
				0x00,  // ASQ: -
				0x00, 0x00, 0x00,  // reserved
				0x00  // additional sense length
			};
		}
	}
	else if (opcode == o_read_capacity_10) {
		DOLOG("scsi::send: READ_CAPACITY\n");
		response.data.second = 8;
		response.data.first = new uint8_t[response.data.second]();
		response.data.first[0] = 0;  // 256 sectors
		response.data.first[1] = 0;
		response.data.first[2] = 1;
		response.data.first[3] = 0;
		response.data.first[4] = 0;  // sector size of 512 bytes
		response.data.first[5] = 0;
		response.data.first[6] = 0x02;
		response.data.first[7] = 0x00;
	}
	else if (opcode == o_get_lba_status) {
		uint64_t starting_lba = (uint64_t(CDB[2]) << 56) | (uint64_t(CDB[3]) << 48) | (uint64_t(CDB[4]) << 40) | (uint64_t(CDB[5]) << 32) | (CDB[6] << 24) | (CDB[7] << 16) | (CDB[8] << 8) | CDB[9];
		uint32_t allocation_length = (CDB[10] << 24) | (CDB[11] << 16) | (CDB[12] << 8) | CDB[13];
		DOLOG("scsi::send: GET_LBA_STATUS, starting lba %llu, allocation length: %u\n", starting_lba, allocation_length);

		response.data.second = 24;
		response.data.first = new uint8_t[response.data.second]();
		response.data.first[0] = 0;  // parameter length
		response.data.first[1] = 0;
		response.data.first[2] = 0;
		response.data.first[3] = 16;
		for(int i=2; i<10; i++)  // LBA status logical block address
			response.data.first[i - 2 + 8] = CDB[i];
		int blocks_required = (allocation_length + 4095) / 4096;
		response.data.first[16] = blocks_required >> 24;
		response.data.first[17] = blocks_required >> 16;
		response.data.first[18] = blocks_required >>  8;
		response.data.first[19] = blocks_required;
	}
	else if (opcode == o_write_10) {
		uint64_t lba = (CDB[2] << 24) | (CDB[3] << 16) | (CDB[4] << 8) | CDB[5];
		uint32_t transfer_length = (CDB[6] << 8) | CDB[7];

		DOLOG("scsi::send: WRITE_10, offset %llu, %u sectors\n", lba, transfer_length);

		if (data.has_value()) {
			DOLOG("scsi::send: write command includes data\n");
			// TODO write data
		}
		else {
			response.type = ir_r2t;  // allow R2T packets to come in
		}
	}
	else if (opcode == o_write_16) {
		uint64_t lba = (uint64_t(CDB[2]) << 56) | (uint64_t(CDB[3]) << 48) | (uint64_t(CDB[4]) << 40) | (uint64_t(CDB[5]) << 32) | (CDB[6] << 24) | (CDB[7] << 16) | (CDB[8] << 8) | CDB[9];
		uint32_t transfer_length = (CDB[10] << 24) | (CDB[11] << 16) | (CDB[12] << 8) | CDB[13];

		DOLOG("scsi::send: WRITE_16, offset %llu, %u sectors\n", lba, transfer_length);

		if (data.has_value()) {
			DOLOG("scsi::send: write command includes data\n");
			// TODO write data
		}
		else {
			response.type = ir_r2t;  // allow R2T packets to come in
		}
	}
	else {
		DOLOG("scsi::send: opcode %02xh not implemented\n", opcode);
		response.sense_data = { 0x70, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00 };
	}

	DOLOG("-> returning %zu bytes of sense data\n", response.sense_data.size());

	if (data.has_value())
		delete [] data.value().first;

	return response;
}
