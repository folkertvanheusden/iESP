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

	scsi_response response { };
	response.type         = ir_as_is;
	response.data_is_meta = true;

	// TODO: if LBA or TRANSFER LENGTH out of range, return error

	if (opcode == o_test_unit_ready) {
		DOLOG("scsi::send: TEST UNIT READY\n");
		response.type = ir_empty_sense;
	}
	else if (opcode == o_mode_sense_6) {  // 0x1a
		DOLOG("scsi::send: MODE SENSE 6\n");
		if (CDB[1] & 8)
			DOLOG(" MODE SENSE 6: DBD\n");
		uint8_t page_control = CDB[2] >> 6;
		const char *const pagecodes[] { "current values", "changeable values", "default values", "saved values " };
		DOLOG(" MODE SENSE 6: PAGE CONTROL %s (%d)\n", pagecodes[page_control], page_control);
		uint8_t page_code = CDB[2] & 0x3f;
		DOLOG(" MODE SENSE 6: PAGE CODE %02xh\n", page_code);
		DOLOG(" MODE SENSE 6: SUBPAGE CODE %02xh\n", CDB[3]);
		DOLOG(" MODE SENSE 6: AllocationLength: %d\n", CDB[4]);
		DOLOG(" MODE SENSE 6: Control: %02xh\n", CDB[5]);
		response.data.second = 4;
		response.data.first = new uint8_t[response.data.second]();
		response.data.first[0] = response.data.second - 1;  // length
	}
	else if (opcode == o_inquiry) {  // 0x12
		DOLOG("scsi::send: INQUIRY\n");
		if (CDB[1] & 1) {
			DOLOG(" INQUIRY: EVPD\n");
			DOLOG(" INQUIRY: PageCode: %02xh\n", CDB[2]);
		}
		if (CDB[1] & 2)
			DOLOG(" INQUIRY: CmdDt\n");
		uint16_t allocation_length = (CDB[3] << 8) | CDB[4];
		DOLOG(" INQUIRY: AllocationLength: %d\n", allocation_length);
		DOLOG(" INQUIRY: ControlByte: %02xh\n", CDB[5]);
		bool ok = true;
		if ((CDB[1] & 1) == 0) {
			if (CDB[2])
				ok = false;
			else {
				response.data.second = 68;
				response.data.first = new uint8_t[response.data.second]();
				response.data.first[0] = 0x00;  // "Direct access block device"
				response.data.first[1] = 0;  // not removable
				response.data.first[2] = 5;  // VERSION
				response.data.first[3] = 2;  // response data format
				response.data.first[4] = response.data.second - 5;  // additional length
				response.data.first[5] = 0;
				response.data.first[6] = 0;
				response.data.first[7] = 0;
				memcpy(&response.data.first[8],  "vnHeusdn", 8);
				memcpy(&response.data.first[16], "iESP", 4);  // TODO
				memcpy(&response.data.first[32], "1.0", 3);  // TODO
				memcpy(&response.data.first[36], "12345678", 8);  // TODO
				response.data.first[58] = 0x04;  // SBC-3
				response.data.first[59] = 0xc0;
				response.data.first[60] = 0x09;  // iSCSI
				response.data.first[61] = 0x60;
				response.data.first[62] = 0x01;  // SCC-2
				response.data.first[63] = 0xfb;
				response.data.first[64] = 0x01;  // SPC
				response.data.first[65] = 0x20;
			}
		}
		else {
			if (CDB[2] == 0x83) {
				response.data.second = 4 + 5;
				response.data.first = new uint8_t[response.data.second]();
				response.data.first[0] = 0;  // TODO
				response.data.first[1] = CDB[2];
				response.data.first[3] = 1;
				response.data.first[4 + 3] = 1;
				response.data.first[4 + 4] = 1;
			}
			else if (CDB[2] == 0xb0) {
				response.data.second = 64;
				response.data.first = new uint8_t[response.data.second]();
				response.data.first[0] = 0;  // TODO
				response.data.first[1] = CDB[2];
				response.data.first[2] = (response.data.second - 4) >> 8;  // page length
				response.data.first[3] = response.data.second - 4;
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
				response.data.first[1] = CDB[2];
				response.data.first[2] = (response.data.second - 4)>> 8;  // page length
				response.data.first[3] = response.data.second - 4;
				response.data.first[4] = 0x1c;  // device has an RPM of 7200 (fake!)
				response.data.first[5] = 0x20;
				// ... set all to 'not set'
			}
			else {
				ok = false;
			}
		}

		if (!ok) {  // PageCode not supported
			response.sense_data = {
				0x72,  // current errors
				0x05,  // key: illegal request
				0x24,  // ASC: invalid field in cdb
				0x00,  // ASQ: -
				0x00, 0x00, 0x00,  // reserved
				0x00  // additional sense length
			};
		}

		response.data.second = std::min(response.data.second, size_t(allocation_length));
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
		uint8_t service_action = CDB[1] & 31;
		DOLOG("  ServiceAction: %02xh\n", service_action);

		if (service_action == 0x10) {  // READ CAPACITY
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
		else if (service_action == 0x12) {  // GET LBA STATUS
			DOLOG("scsi::send: GET_LBA_STATUS\n");

			response.data.second = 24;
			response.data.first = new uint8_t[response.data.second]();
			response.data.first[0] = 0;
			response.data.first[1] = 0;
			response.data.first[2] = 0;
			response.data.first[3] = response.data.second - 3;
			memcpy(&response.data.first[8], &CDB[2], 8);  // LBA STATUS LOGICAL BLOCK ADDRESS
			memcpy(&response.data.first[16], &CDB[10], 4);  // ALLOCATIN LENGTH
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

			auto   backend_block_size = b->get_block_size();
			size_t expected_size   = transfer_length * backend_block_size;
			size_t received_size   = data.value().second;
			size_t received_blocks = received_size / backend_block_size;
			if (received_blocks == 0 || b->write(lba, transfer_length, data.value().first) == false) {
				DOLOG("scsi::send: WRITE_xx, failed writing\n");

				// TODO set sense_data
			}
			else {
				if (received_size == expected_size)
					response.type = ir_empty_sense;
				else {  // allow R2T packets to come in
					response.type = ir_r2t;

					response.r2t.buffer_lba      = lba;
					response.r2t.offset_from_lba = received_blocks * backend_block_size;
					response.r2t.bytes_left      = (transfer_length - received_blocks) * backend_block_size;
					DOLOG("scsi::send: starting R2T with %u blocks left (LBA: %llu, offset %u)\n", response.r2t.bytes_left, response.r2t.buffer_lba, response.r2t.offset_from_lba);
				}
			}
		}
		else {
			response.type = ir_r2t;  // allow R2T packets to come in
		}
	}
	else if (opcode == o_read_16 || opcode == o_read_10 || opcode == o_read_6) {  // 0x88, 0x28, 0x08
		uint64_t lba             = 0;
		uint32_t transfer_length = 0;

		if (opcode == o_read_16) {
			lba             = (uint64_t(CDB[2]) << 56) | (uint64_t(CDB[3]) << 48) | (uint64_t(CDB[4]) << 40) | (uint64_t(CDB[5]) << 32) | (CDB[6] << 24) | (CDB[7] << 16) | (CDB[8] << 8) | CDB[9];
			transfer_length = (CDB[10] << 24) | (CDB[11] << 16) | (CDB[12] << 8) | CDB[13];
			DOLOG("scsi::send: READ_16, offset %llu, %u sectors\n", lba, transfer_length);
		}
		else if (opcode == o_read_10) {
			lba             = (uint64_t(CDB[2]) << 24) | (uint64_t(CDB[3]) << 16) | (uint64_t(CDB[4]) << 8) | uint64_t(CDB[5]);
			transfer_length =  (CDB[7] << 8) | CDB[8];
			DOLOG("scsi::send: READ_10, offset %llu, %u sectors\n", lba, transfer_length);
		}
		else {
			lba             = ((CDB[1] & 31) << 16) | (CDB[2] << 8) | CDB[3];
			transfer_length = CDB[4];
			DOLOG("scsi::send: READ_6, offset %llu, %u sectors\n", lba, transfer_length);
		}

		response.data.second = transfer_length * b->get_block_size();
		if (response.data.second) {
			response.data.first = new uint8_t[response.data.second]();
			if (b->read(lba, transfer_length, response.data.first) == false) {
				DOLOG("scsi::send: READ_xx, failed reading\n");

				delete [] response.data.first;
				response.data.first  = nullptr;
				response.data.second = 0;

				// TODO set sense_data;
			}
		}
		response.data_is_meta = false;
	}
	else if (opcode == o_report_luns) {  // 0xA0
		DOLOG("scsi::send: REPORT_LUNS, report: %02xh\n", CDB[2]);

		response.data.second = 16;
		response.data.first = new uint8_t[response.data.second]();
		response.data.first[3] = 1;  // lun list length of 1
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
