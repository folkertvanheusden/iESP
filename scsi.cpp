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
	printf("SCSI opcode: %02xh\n", opcode);

	scsi_response response { };

	if (opcode == o_test_unit_ready) {
	}
	else if (opcode == o_inquiry) {
		DOLOG("scsi::send: inquiry\n");
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
	else if (opcode == o_read_capacity) {
		DOLOG("scsi::send: read_capacity\n");
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
		DOLOG("scsi::send: get_lba_status(CDB size: %zu), starting lba %llu, allocation length: %u\n", size, starting_lba, allocation_length);

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
	else if (opcode == o_write_16) {
		uint64_t lba = (uint64_t(CDB[2]) << 56) | (uint64_t(CDB[3]) << 48) | (uint64_t(CDB[4]) << 40) | (uint64_t(CDB[5]) << 32) | (CDB[6] << 24) | (CDB[7] << 16) | (CDB[8] << 8) | CDB[9];
		uint32_t transfer_length = (CDB[10] << 24) | (CDB[11] << 16) | (CDB[12] << 8) | CDB[13];

		DOLOG("scsi::send: write_16(CDB size: %zu), offset %llu, %u sectors\n", size, lba, transfer_length);
		// TODO
	}
	else {
		DOLOG("scsi::send: opcode %02xh not implemented\n", opcode);
		response.sense_data = { 0x70, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00 };
	}

	printf("-> returning %zu bytes of sense data\n", response.sense_data.size());

	return response;
}
