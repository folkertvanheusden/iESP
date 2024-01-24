#include <cassert>
#include <cstdio>
#include <cstring>

#include "log.h"
#include "scsi.h"
#include "utils.h"


scsi::scsi(backend *const b) : b(b)
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
		else {
			if (CDB[2] == 0xb0) {
				response.data.second = 64;
				response.data.first = new uint8_t[response.data.second]();
				response.data.first[0] = 0;  // TODO
				response.data.first[1] = 0xb0;
				response.data.first[2] = response.data.second >> 8;  // page length
				response.data.first[3] = response.data.second;
				response.data.first[4] = 0;  // WSNZ bit
				response.data.first[5] = 0;  // compare and write not supported
				response.data.first[6] = 0;  // OPTIMAL TRANSFER LENGTH GRANULARITY
				response.data.first[7] = 0;
				// ... set all to 'not set'
			}
			else if (CDB[2] == 0xb1) {
				response.data.second = 64;
				response.data.first = new uint8_t[response.data.second]();
				response.data.first[0] = 0;  // TODO
				response.data.first[1] = 0xb1;
				response.data.first[2] = response.data.second >> 8;  // page length
				response.data.first[3] = response.data.second;
				response.data.first[4] = 0x1c;  // device has an RPM of 7200 (fake!)
				response.data.first[5] = 0x20;
				// ... set all to 'not set'
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
	}
	else if (opcode == o_read_capacity_10) {
		DOLOG("scsi::send: READ_CAPACITY\n");
		response.data.second = 8;
		response.data.first = new uint8_t[response.data.second]();
		auto device_size = b->get_size_in_blocks();
		response.data.first[0] = device_size >> 24;  // sector count
		response.data.first[1] = device_size >> 16;
		response.data.first[2] = device_size >>  8;
		response.data.first[3] = device_size;
		auto block_size = b->get_block_size();
		response.data.first[4] = block_size >> 24;  // sector size
		response.data.first[5] = block_size >> 16;
		response.data.first[6] = block_size >>  8;
		response.data.first[7] = block_size;
	}
	else if (opcode == o_get_lba_status) {
		DOLOG("  ServiceAction: %02xh\n", CDB[1] & 31);

		if ((CDB[1] & 31) == 0x10) {  // READ CAPACITY
			DOLOG("scsi::send: READ_CAPACITY(16)\n");

			response.data.second = 32;
			response.data.first = new uint8_t[response.data.second]();
			auto device_size = b->get_size_in_blocks();
			response.data.first[0] = device_size >> 56;
			response.data.first[1] = device_size >> 48;
			response.data.first[2] = device_size >> 40;
			response.data.first[3] = device_size >> 32;
			response.data.first[4] = device_size >> 24;
			response.data.first[5] = device_size >> 16;
			response.data.first[6] = device_size >>  8;
			response.data.first[7] = device_size;
			uint32_t block_size = b->get_block_size();
			response.data.first[8] = block_size >> 24;
			response.data.first[9] = block_size >> 16;
			response.data.first[10] = block_size >>  8;
			response.data.first[11] = block_size;
			response.data.first[12] = 1 << 4;  // RC BASIS: "The RETURNED LOGICAL BLOCK ADDRESS field indicates the LBA of the last logical block on the logical unit."
		}
	}
	else if (opcode == o_write_10 || opcode == o_write_16) {
		uint64_t lba             = 0;
		uint32_t transfer_length = 0;

		if (opcode == o_write_10) {
			lba             = (CDB[2] << 24) | (CDB[3] << 16) | (CDB[4] << 8) | CDB[5];
			transfer_length = (CDB[6] << 8) | CDB[7];
		}
		else if (opcode == o_write_16) {
			lba             = (uint64_t(CDB[2]) << 56) | (uint64_t(CDB[3]) << 48) | (uint64_t(CDB[4]) << 40) | (uint64_t(CDB[5]) << 32) | (CDB[6] << 24) | (CDB[7] << 16) | (CDB[8] << 8) | CDB[9];
			transfer_length = (CDB[10] << 24) | (CDB[11] << 16) | (CDB[12] << 8) | CDB[13];
		}
		else {
			DOLOG("scsi::send: WRITE_1x internal error\n");
		}

		DOLOG("scsi::send: WRITE_1%c, offset %llu, %u sectors\n", opcode == o_write_10 ? '0' : '6', lba, transfer_length);

		if (data.has_value()) {
			DOLOG("scsi::send: write command includes data\n");

			if (transfer_length * b->get_block_size() == data.value().second)
				b->write(lba, transfer_length, data.value().first);
			else
				DOLOG("scsi::send: write did not receive all/more data\n");
		}
		else {
			response.type = ir_r2t;  // allow R2T packets to come in
		}
	}
	else if (opcode == o_read_16) {  // 0x88
		uint64_t lba = (uint64_t(CDB[2]) << 56) | (uint64_t(CDB[3]) << 48) | (uint64_t(CDB[4]) << 40) | (uint64_t(CDB[5]) << 32) | (CDB[6] << 24) | (CDB[7] << 16) | (CDB[8] << 8) | CDB[9];
		uint32_t transfer_length = (CDB[10] << 24) | (CDB[11] << 16) | (CDB[12] << 8) | CDB[13];

		DOLOG("scsi::send: READ_16, offset %llu, %u sectors\n", lba, transfer_length);

		response.data.second = transfer_length * b->get_block_size();
		response.data.first = new uint8_t[response.data.second]();
		b->read(lba, transfer_length, response.data.first);
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
