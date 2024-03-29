#ifdef TEENSY4_1
#include <Arduino.h>
#endif
#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <thread>
#ifdef ESP32
#include <Esp.h>
#endif

#include "log.h"
#include "scsi.h"
#include "utils.h"


typedef struct {
	std::vector<uint8_t> data;
	int cdb_length;
} scsi_opcode_details;

const std::map<scsi::scsi_opcode, scsi_opcode_details> scsi_a3_data {
	{ scsi::scsi_opcode::o_test_unit_ready, { { 0xff, 0x00, 0x00, 0x00, 0x00, 0x07 }, 6 } },
	{ scsi::scsi_opcode::o_request_sense,   { { 0xff, 0x01, 0x00, 0x00, 0xff, 0x07 }, 6 } },
	{ scsi::scsi_opcode::o_read_6,		{ { 0xff, 0x1f, 0xff, 0xff, 0xff, 0x07 }, 6 } },
	{ scsi::scsi_opcode::o_write_6,		{ { 0xff, 0x1f, 0xff, 0xff, 0xff, 0x07 }, 6 } },
//	{ scsi::scsi_opcode::o_seek,		{
	{ scsi::scsi_opcode::o_inquiry,		{ { 0xff, 0x01, 0xff, 0xff, 0xff, 0x07 }, 6 } },
	{ scsi::scsi_opcode::o_reserve_6,	{ { 0xff, 0x00, 0x00, 0x00, 0x00, 0x07 }, 6 } },
	{ scsi::scsi_opcode::o_release_6,	{ { 0xff, 0x00, 0x00, 0x00, 0x00, 0x07 }, 6 } },
	{ scsi::scsi_opcode::o_mode_sense_6,	{ { 0xff, 0x08, 0xff, 0xff, 0xff, 0x07 }, 6 } },
	{ scsi::scsi_opcode::o_read_capacity_10,{ { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07 }, 10 } },
	{ scsi::scsi_opcode::o_read_10,		{ { 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0x07 }, 10 } },
	{ scsi::scsi_opcode::o_write_10,	{ { 0xff, 0xfa, 0xff, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0x07 }, 10 } },
	{ scsi::scsi_opcode::o_write_verify_10,	{ { 0xff, 0xf2, 0xff, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0x07 }, 10 } },
	{ scsi::scsi_opcode::o_sync_cache_10,	{ { 0xff, 0x06, 0xff, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0x07 }, 10 } },
	{ scsi::scsi_opcode::o_unmap,           { { 0xff, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x07 }, 10 } },
	{ scsi::scsi_opcode::o_read_16,		{ { 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x07 }, 16 } },
	{ scsi::scsi_opcode::o_compare_and_write, { { 0xff, 0xfa, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00, 0x07 }, 16 } },
	{ scsi::scsi_opcode::o_write_16,	{ { 0xff, 0xfa, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x07 }, 16 } },
	{ scsi::scsi_opcode::o_get_lba_status,	{ { 0xff, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x07 }, 16 } },
	{ scsi::scsi_opcode::o_report_luns,	{ { 0xff, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x07 }, 12 } },
	{ scsi::scsi_opcode::o_rep_sup_oper,	{ { 0xff, 0x1f, 0x87, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x07 }, 12 } },
};

constexpr const uint8_t max_compare_and_write_block_count = 1;

scsi::scsi(backend *const b, const int trim_level, io_stats_t *const is) : b(b), trim_level(trim_level), is(is)
{
#ifdef ESP32
	uint64_t temp = ESP.getEfuseMac();
	serial = myformat("%" PRIx64, temp);
#elif defined(TEENSY4_1)
        uint32_t m1 = HW_OCOTP_MAC1;
        uint32_t m2 = HW_OCOTP_MAC0;
	serial = myformat("%08x%08x", m1, m2);
#else
	FILE *fh = fopen("/var/lib/dbus/machine-id", "r");
	if (fh) {
		char buffer[128] { 0 };
		fgets(buffer, sizeof buffer, fh);
		fclose(fh);
		char *lf = strchr(buffer, '\n');
		if (lf)
			*lf = 0x00;
		serial = buffer;
	}
	else {
		serial = "12345678";
	}
#endif
}

scsi::~scsi()
{
}

std::optional<scsi_response> scsi::send(const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data)
{
	assert(size >= 16);

	scsi_opcode opcode = scsi_opcode(CDB[0]);
	DOLOG("SCSI opcode: %02xh, CDB size: %zu\n", opcode, size);
	DOLOG("CDB contents: %s\n", to_hex(CDB, size).c_str());

	scsi_response response { };
	response.type         = ir_as_is;
	response.data_is_meta = true;

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
		response.io.is_inline          = true;
		response.io.what.data.second   = 4;
		response.io.what.data.first    = new uint8_t[response.io.what.data.second]();
		response.io.what.data.first[0] = response.io.what.data.second - 1;  // length
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
		uint8_t device_type = lun == 0 ? 0x0c :  // storage array controller
						 0x00;  // direct access block device
		if ((CDB[1] & 1) == 0) {  // requests standard inquiry data
			if (CDB[2])
				ok = false;
			else {
				response.io.is_inline        = true;
				response.io.what.data.second = 68;
				response.io.what.data.first  = new uint8_t[response.io.what.data.second]();
				response.io.what.data.first[0] = device_type;
				response.io.what.data.first[1] = 0;  // not removable
				response.io.what.data.first[2] = 5;  // VERSION
				response.io.what.data.first[3] = 2;  // response data format
				response.io.what.data.first[4] = response.io.what.data.second - 5;  // additional length
				response.io.what.data.first[5] = 0;
				response.io.what.data.first[6] = 0;
				response.io.what.data.first[7] = 0;
				memcpy(&response.io.what.data.first[8],  "vnHeusdn", 8);
				memcpy(&response.io.what.data.first[16], "iESP", 4);
				memcpy(&response.io.what.data.first[32], "1.0", 3);  // TODO
				memset(&response.io.what.data.first[36], '0', 8);
				memcpy(&response.io.what.data.first[36], serial.c_str(), std::min(serial.size(), size_t(8)));
				response.io.what.data.first[58] = 0x06;  // SBC-4
				response.io.what.data.first[59] = 0x00;
				response.io.what.data.first[60] = 0x09;  // iSCSI
				response.io.what.data.first[61] = 0x60;
				response.io.what.data.first[62] = 0x01;  // SCC-2
				response.io.what.data.first[63] = 0xfb;
				response.io.what.data.first[64] = 0x01;  // SPC
				response.io.what.data.first[65] = 0x20;
			}
		}
		else {
                        if (CDB[2] == 0x00) {  // supported vital product page
				response.io.is_inline          = true;
                                response.io.what.data.second   = 9;
                                response.io.what.data.first    = new uint8_t[response.io.what.data.second]();
				response.io.what.data.first[0] = device_type;
                                response.io.what.data.first[1] = CDB[2];
                                response.io.what.data.first[2] = 0;  // reserved
                                response.io.what.data.first[3] = response.io.what.data.second - 4;
                                response.io.what.data.first[4] = 0x00;
                                response.io.what.data.first[5] = 0x80;
                                response.io.what.data.first[6] = 0x83;  // see CDB[2] below
                                response.io.what.data.first[7] = 0xb0;
                                response.io.what.data.first[8] = 0xb1;
                        }
			else if (CDB[2] == 0x80) {  // unit serial number page
				response.io.is_inline          = true;
				response.io.what.data.second   = 4 + serial.size();
				response.io.what.data.first    = new uint8_t[response.io.what.data.second]();
				response.io.what.data.first[0] = device_type;
				response.io.what.data.first[1] = CDB[2];
				response.io.what.data.first[3] = response.io.what.data.second - 4;
				memcpy(&response.io.what.data.first[4], serial.c_str(), serial.size());
			}
			else if (CDB[2] == 0x83) {  // device identification page
				response.io.is_inline          = true;
				response.io.what.data.second   = 8 + serial.size();
				response.io.what.data.first    = new uint8_t[response.io.what.data.second]();
				response.io.what.data.first[0] = device_type;
				response.io.what.data.first[1] = CDB[2];
				response.io.what.data.first[3] = response.io.what.data.second - 4;
				response.io.what.data.first[4] = 2 | (5 << 4);  // 2 = ascii, 5 = iscsi
				response.io.what.data.first[5] = 128;  // PIV
				response.io.what.data.first[7] = serial.size();
				memcpy(&response.io.what.data.first[8], serial.c_str(), serial.size());
			}
			else if (CDB[2] == 0xb0) {  // block limits
				response.io.is_inline          = true;
				response.io.what.data.second = 64;
				response.io.what.data.first = new uint8_t[response.io.what.data.second]();
				response.io.what.data.first[0] = device_type;
				response.io.what.data.first[1] = CDB[2];
				response.io.what.data.first[2] = 0;
				response.io.what.data.first[3] = 0x3c;  // page length
				response.io.what.data.first[4] = 0;  // WSNZ bit
				response.io.what.data.first[5] = max_compare_and_write_block_count;  // compare and write
				response.io.what.data.first[6] = 0;  // OPTIMAL TRANSFER LENGTH GRANULARITY
				response.io.what.data.first[7] = 0;
#ifdef ESP32
				response.io.what.data.first[22] = 1;  // 'MAXIMUM UNMAP LBA COUNT': 256 blocks
#else
				response.io.what.data.first[22] = 32;  // 'MAXIMUM UNMAP LBA COUNT': 8192 blocks
#endif
				response.io.what.data.first[23] = 00;  // LSB of 'MAXIMUM UNMAP LBA COUNT'
				response.io.what.data.first[27] = 8;  // LSB of 'MAXIMUM UNMAP BLOCK DESCRIPTOR COUNT'
				response.io.what.data.first[31] = 8;  // LSB of 'OPTIMAL UNMAP GRANULARITY'
				// ... set rest to 'not set'
			}
			else if (CDB[2] == 0xb1) {  // block device characteristics
				response.io.is_inline          = true;
				response.io.what.data.second = 64;
				response.io.what.data.first = new uint8_t[response.io.what.data.second]();
				response.io.what.data.first[0] = device_type;
				response.io.what.data.first[1] = CDB[2];
				response.io.what.data.first[2] = (response.io.what.data.second - 4)>> 8;  // page length
				response.io.what.data.first[3] = response.io.what.data.second - 4;
				response.io.what.data.first[4] = 0x1c;  // device has an RPM of 7200 (fake!)
				response.io.what.data.first[5] = 0x20;
				// ... set all to 'not set'
			}
			else {
				errlog("scsi::send: INQUIRY page code %02xh not implemented", CDB[2]);
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

		response.io.what.data.second = std::min(response.io.what.data.second, size_t(allocation_length));
	}
	else if (opcode == o_read_capacity_10) {
		DOLOG("scsi::send: READ_CAPACITY\n");
		response.io.is_inline          = true;
		response.io.what.data.second = 8;
		response.io.what.data.first = new uint8_t[response.io.what.data.second]();
		auto device_size = b->get_size_in_blocks() - 1;
		response.io.what.data.first[0] = device_size >> 24;  // sector count
		response.io.what.data.first[1] = device_size >> 16;
		response.io.what.data.first[2] = device_size >>  8;
		response.io.what.data.first[3] = device_size;
		auto block_size = b->get_block_size();
		response.io.what.data.first[4] = block_size >> 24;  // sector size
		response.io.what.data.first[5] = block_size >> 16;
		response.io.what.data.first[6] = block_size >>  8;
		response.io.what.data.first[7] = block_size;
	}
	else if (opcode == o_get_lba_status) {
		uint8_t service_action = CDB[1] & 31;
		DOLOG("  ServiceAction: %02xh\n", service_action);

		if (service_action == 0x10) {  // READ CAPACITY
			DOLOG("scsi::send: READ_CAPACITY(16)\n");

			uint32_t allocation_length = get_uint32_t(&CDB[10]);

			if (allocation_length == 0)
				response.type = ir_empty_sense;
			else {
				response.io.is_inline          = true;
				response.io.what.data.second   = 32;
				response.io.what.data.first    = new uint8_t[response.io.what.data.second]();
				auto device_size = b->get_size_in_blocks() - 1;
				response.io.what.data.first[0] = device_size >> 56;
				response.io.what.data.first[1] = device_size >> 48;
				response.io.what.data.first[2] = device_size >> 40;
				response.io.what.data.first[3] = device_size >> 32;
				response.io.what.data.first[4] = device_size >> 24;
				response.io.what.data.first[5] = device_size >> 16;
				response.io.what.data.first[6] = device_size >>  8;
				response.io.what.data.first[7] = device_size;
				uint32_t block_size = b->get_block_size();
				response.io.what.data.first[8] = block_size >> 24;
				response.io.what.data.first[9] = block_size >> 16;
				response.io.what.data.first[10] = block_size >>  8;
				response.io.what.data.first[11] = block_size;
				response.io.what.data.first[12] = 1 << 4;  // RC BASIS: "The RETURNED LOGICAL BLOCK ADDRESS field indicates the LBA of the last logical block on the logical unit."
				response.io.what.data.first[14] = 128 | 64;  // LBPME (Logical Block Provisioning Management Enabled), LBPRZ (Logical Block Provisioning Read Zeros)
				response.io.what.data.second = std::min(response.io.what.data.second, size_t(allocation_length));
			}
		}
		else if (service_action == 0x12) {  // GET LBA STATUS
			DOLOG("scsi::send: GET_LBA_STATUS\n");

			uint64_t lba             = get_uint64_t(&CDB[2]);
			uint32_t transfer_length = get_uint32_t(&CDB[10]);

			auto vr = validate_request(lba, transfer_length);
			if (vr.has_value()) {
				errlog("scsi::send: GET LBA STATUS parameters invalid");
				response.sense_data = vr.value();
			}
			else {
				response.io.is_inline          = true;
				response.io.what.data.second   = 24;
				response.io.what.data.first    = new uint8_t[response.io.what.data.second]();
				response.io.what.data.first[0] = 0;
				response.io.what.data.first[1] = 0;
				response.io.what.data.first[2] = 0;
				response.io.what.data.first[3] = response.io.what.data.second - 3;
				memcpy(&response.io.what.data.first[8], &CDB[2], 8);  // LBA STATUS LOGICAL BLOCK ADDRESS
				memcpy(&response.io.what.data.first[16], &CDB[10], 4);  // ALLOCATION LENGTH
			}
		}
		else {
			errlog("scsi::send: GET LBA STATUS service action %02xh not implemented", service_action);
			response.sense_data = error_not_implemented();
		}
	}
	else if (opcode == o_write_6 || opcode == o_write_10 || opcode == o_write_verify_10 || opcode == o_write_16) {
		uint64_t lba             = 0;
		uint32_t transfer_length = 0;

		if (opcode == o_write_6) {
			lba             = ((CDB[1] & 31) << 16) | (CDB[2] << 8) | CDB[3];
			transfer_length = CDB[4];
			if (transfer_length == 0)
				transfer_length = 256;
		}
		else if (opcode == o_write_10 || opcode == o_write_verify_10) {
			// NOTE: the verify part is not implemented, o_write_verify_10 is just a dumb write
			lba             = get_uint32_t(&CDB[2]);
			transfer_length = (CDB[7] << 8) | CDB[8];
		}
		else if (opcode == o_write_16) {
			lba             = get_uint64_t(&CDB[2]);
			transfer_length = get_uint32_t(&CDB[10]);
		}

		DOLOG("scsi::send: WRITE_1%c, offset %" PRIu64 ", %u sectors\n", opcode == o_write_10 ? '0' : '6', lba, transfer_length);

		auto vr = validate_request(lba, transfer_length);
		if (vr.has_value()) {
			errlog("scsi::send: WRITE_1x parameters invalid");
			response.sense_data = vr.value();
		}
		else if (data.first) {
			DOLOG("scsi::send: write command includes data (%zu bytes)\n", data.second);

			auto   backend_block_size = b->get_block_size();
			size_t expected_size      = transfer_length * backend_block_size;
			size_t received_size      = data.second;
			size_t received_blocks    = received_size / backend_block_size;
			if (received_blocks)
				DOLOG("scsi::send: WRITE_xx to LBA %" PRIu64 " is %zu in bytes, %zu bytes\n", lba, lba * backend_block_size, received_size);

			bool ok = true;
			if (received_blocks > 0) {
				auto rc = write(lba, received_blocks, data.first);

				if (rc == scsi_rw_result::rw_fail_general) {
					errlog("scsi::send: WRITE_xx, general write error");
					response.sense_data = error_write_error();
					ok = false;
				}
				else if (rc == rw_fail_locked) {
					errlog("scsi::send: WRITE_xx, failed writing due to reservations");
					response.sense_data = error_reserve_6();
					ok = false;
				}
			}

			if (ok) {
				if (received_size == expected_size) {
					response.type = ir_empty_sense;
					DOLOG("scsi::send: received_size == expected_size\n");
				}
				else {  // allow R2T packets to come in
					response.type = ir_r2t;

					response.r2t.buffer_lba      = lba;
					response.r2t.bytes_left      = (transfer_length - received_blocks) * backend_block_size;
					response.r2t.bytes_done      = received_blocks * backend_block_size;
					DOLOG("scsi::send: starting R2T with %u bytes left (LBA: %" PRIu64 ", offset %u)\n", response.r2t.bytes_left, response.r2t.buffer_lba, response.r2t.bytes_done);
				}
			}
		}
		else {
			DOLOG("scsi::send: WRITE without data\n");

			if (transfer_length)
				response.type = ir_r2t;  // allow R2T packets to come in
			else {
				response.type = ir_empty_sense;
				DOLOG("scsi::send: WRITE with 0 transfer_length\n");
			}
		}
	}
	else if (opcode == o_read_16 || opcode == o_read_10 || opcode == o_read_6) {  // 0x88, 0x28, 0x08
		uint64_t lba             = 0;
		uint32_t transfer_length = 0;

		if (opcode == o_read_16) {
			lba             = get_uint64_t(&CDB[2]);
			transfer_length = get_uint32_t(&CDB[10]);
			DOLOG("scsi::send: READ_16, LBA %" PRIu64 ", %u sectors\n", lba, transfer_length);
		}
		else if (opcode == o_read_10) {
			lba             = get_uint32_t(&CDB[2]);
			transfer_length =  (CDB[7] << 8) | CDB[8];
			DOLOG("scsi::send: READ_10, LBA %" PRIu64 ", %u sectors\n", lba, transfer_length);
		}
		else {
			lba             = ((CDB[1] & 31) << 16) | (CDB[2] << 8) | CDB[3];
			transfer_length = CDB[4];
			if (transfer_length == 0)
				transfer_length = 256;
			DOLOG("scsi::send: READ_6, LBA %" PRIu64 ", %u sectors\n", lba, transfer_length);
		}

		auto vr = validate_request(lba, transfer_length);
		if (vr.has_value()) {
			errlog("scsi::send: READ_1x parameters invalid");
			response.sense_data = vr.value();
		}
		else if (transfer_length == 0) {
			DOLOG("scsi::send: READ_1x 0-read\n");
			response.type = ir_empty_sense;
		}
		else {
			response.io.is_inline               = false;
			response.io.what.location.lba       = lba;
			response.io.what.location.n_sectors = transfer_length;
			response.io.what.data.first         = nullptr;

			response.data_is_meta = false;
		}
	}
	else if (opcode == o_sync_cache_10) {  // 0x35
		DOLOG("scsi::send: SYNC CACHE 10\n");

		response.type = ir_empty_sense;
	}
	else if (opcode == o_report_luns) {  // 0xa0
		DOLOG("scsi::send: REPORT_LUNS, report: %02xh\n", CDB[2]);

		response.io.is_inline           = true;
		response.io.what.data.second    = 16;
		response.io.what.data.first     = new uint8_t[response.io.what.data.second]();
		response.io.what.data.first[3]  = 8;  // lun list length
		// 4...7 reserved
		encode_lun(&response.io.what.data.first[8], 1);  // LUN1
	}
	else if (opcode == o_rep_sup_oper) {  // 0xa3
		uint8_t service_action     = CDB[1] & 31;
		uint8_t reporting_options  = CDB[2] & 7;
		uint8_t req_operation_code = CDB[3];
		DOLOG("scsi::send: REPORT SUPPORTED OPERATION CODES, service action %02xh, requested operation code %02xh, reporting options: %xh\n", service_action, req_operation_code, reporting_options);
		bool ok = false;

		if (service_action == 0x0c) {
			if (reporting_options == 0) {  // all
				size_t n_elements = scsi_a3_data.size();
				size_t total_size = n_elements * 8;

				response.io.is_inline           = true;
				response.io.what.data.second    = 4 + total_size;
				response.io.what.data.first     = new uint8_t[response.io.what.data.second]();
				response.io.what.data.first[0] = (total_size + 0) >> 24;
				response.io.what.data.first[1] = (total_size + 0) >> 16;
				response.io.what.data.first[2] = (total_size + 0) >>  8;
				response.io.what.data.first[3] = (total_size + 0);

				assert(total_size < 256);

				size_t add_offset = 0;
				for(auto & it: scsi_a3_data) {
					size_t offset = 4 + add_offset;
					response.io.what.data.first[offset + 0] = it.first;
					response.io.what.data.first[offset + 7] = it.second.cdb_length;
					add_offset += 8;
				}

				ok = true;
			}
			else if (reporting_options == 1) {  // one
				auto it = scsi_a3_data.find(scsi::scsi_opcode(req_operation_code));
				if (it != scsi_a3_data.end()) {
					response.io.is_inline           = true;
					response.io.what.data.second    = 4 + it->second.data.size();
					response.io.what.data.first     = new uint8_t[response.io.what.data.second]();
					response.io.what.data.first[1]  = 3 + it->second.data.size();
					memcpy(response.io.what.data.first + 4, it->second.data.data(), it->second.data.size());

					ok = true;
				}
				else {
					errlog("scsi::send: 0xa3 for opcode %02x not implemented", req_operation_code);
				}
			}
			else {
				errlog("scsi::send: 0xa3 reporting option %d not implemented", reporting_options);
			}
		}
		else {
			errlog("scsi::send: 0xa3 service action %02x not implemented", service_action);
		}

		if (!ok)
			response.sense_data = error_not_implemented();
	}
	else if (opcode == o_compare_and_write) {  // 0x89
		uint64_t lba         = get_uint64_t(&CDB[2]);
		uint32_t block_count = CDB[13];
		DOLOG("scsi::send: COMPARE AND WRITE: LBA %" PRIu64 ", transfer length: %u\n", lba, block_count);

		auto block_size = b->get_block_size();
		auto expected_data_size = block_size * block_count * 2;
		if (expected_data_size != data.second)
			DOLOG("scsi::send: COMPARE AND WRITE: data count mismatch (%zu versus %zu)\n", size_t(expected_data_size), data.second);

		auto vr = validate_request(lba, block_count);
		if (vr.has_value()) {
			DOLOG("scsi::send: COMPARE AND WRITE parameters invalid\n");
			response.sense_data = vr.value();
		}
		else if (block_count > max_compare_and_write_block_count) {
			DOLOG("scsi::send: COMPARE AND WRITE: too many blocks in one go (%u versus %u)\n", block_count, max_compare_and_write_block_count);
			response.sense_data = error_compare_and_write_count();
		}
		else {
			scsi_rw_result match = rw_ok;
			uint8_t *buffer = new uint8_t[block_size]();
			for(uint32_t i=0; i<block_count; i++) {
				match = read(lba + i, 1, buffer);

				if (match != rw_ok) {
					errlog("scsi::send: read from backend error");
					break;
				}

				if (memcmp(buffer, &data.first[i * block_size], block_size) != 0) {
					match = rw_fail_general;
					errlog("scsi::send: block %u (LBA: %" PRIu64 ") mismatch", i, lba + i);
					break;
				}
			}
			delete [] buffer;

			if (match == rw_ok) {
				scsi_rw_result write_result = rw_ok;

				for(uint32_t i=0; i<block_count; i++) {
					write_result = write(lba + i, 1, &data.first[i * block_size + block_count * block_size]);

					if (write_result != rw_ok) {
						errlog("scsi::send: write to backend error");
						break;
					}
				}

				if (write_result == rw_ok)
					response.type = ir_empty_sense;
				else if (write_result == rw_fail_locked)
					response.sense_data = error_reserve_6();
				else
					response.sense_data = error_write_error();
			}
			else if (match == rw_fail_locked)
				response.sense_data = error_reserve_6();
			else {
				response.sense_data = error_miscompare();
			}
		}
	}
	else if (opcode == o_prefetch_10 || opcode == o_prefetch_16) {  // 0x34 & 0x90
		DOLOG("scsi::send: PREFETCH 10/16\n");
		response.type = ir_empty_sense;
	}
	else if (opcode == o_reserve_6) {
		DOLOG("scsi::send: RESERVE 6\n");
		if (reserve_device() == l_locked)
			response.type = ir_empty_sense;
		else
			response.sense_data = error_reserve_6();
	}
	else if (opcode == o_release_6) {
		DOLOG("scsi::send: RELEASE 6\n");
		if (unlock_device())
			response.type = ir_empty_sense;
		else
			response.sense_data = error_reserve_6();
	}
	else if (opcode == o_unmap) {
		DOLOG("scsi::send: UNMAP\n");
#ifndef NDEBUG
		DOLOG("GREP unmap CDB size %zu\n", size);
		DOLOG("GREP unmap data size %zu\n", data.second);
		uint16_t parameter_list_length = (CDB[7] << 8) | CDB[8];
		DOLOG("GREP parameter_list_length %u\n", parameter_list_length);
		uint16_t unmap_data_length = (CDB[10 + 0] << 8) | CDB[10 + 1];
		DOLOG("GREP unmap_data_length %u\n", unmap_data_length);
		uint16_t unmap_block_descriptor_data_length = (CDB[10 + 2] << 8) | CDB[10 + 3];
		DOLOG("GREP unmap_block_descriptor_data_length %u\n", unmap_block_descriptor_data_length);
#endif
		const uint8_t *const pd = data.first;
		scsi_rw_result rc = rw_ok;
		for(size_t i=8; i<data.second; i+= 16) {
			uint64_t lba            = get_uint64_t(&pd[i]);
			uint32_t transfer_length = get_uint32_t(&pd[i + 8]);

			auto vr = validate_request(lba, transfer_length);
			if (vr.has_value() || transfer_length > 8192) {  // sanity check TODO hardcoded limit
				errlog("scsi::send: UNMAP parameters invalid");
				response.sense_data = vr.value();
				rc = rw_fail_general;
			}
			else {
				DOLOG("scsi::send: UNMAP trim LBA %" PRIu64 ", %u blocks\n", lba, transfer_length);

				rc = trim(lba, transfer_length);
				if (rc != rw_ok) {
					errlog("scsi::send: UNMAP trim failed");
					break;
				}
			}
		}

		if (rc == rw_ok)
			response.type = ir_empty_sense;
		else if (response.sense_data.empty() == false) {
			// error already set
		}
		else if (rc == rw_fail_locked)
			response.sense_data = error_reserve_6();
		else {
			response.sense_data = error_write_error();
		}
	}
	else {
		errlog("scsi::send: opcode %02xh not implemented", opcode);
		response.sense_data = error_not_implemented();
	}

	DOLOG("-> returning %zu bytes of sense data\n", response.sense_data.size());

	return response;
}

// returns sense data in case of a problem
std::optional<std::vector<uint8_t> > scsi::validate_request(const uint64_t lba, const uint32_t n_blocks) const
{
	auto size_in_blocks = get_size_in_blocks();

	if (lba + n_blocks > size_in_blocks || lba + n_blocks < lba)
		return error_out_of_range();

	return { };  // no error
}

uint64_t scsi::get_size_in_blocks() const
{
	return b->get_size_in_blocks();
}

uint64_t scsi::get_block_size() const
{
	return b->get_block_size();
}

scsi::scsi_rw_result scsi::sync()
{
	if (locking_status() != l_locked_other) {  // locked by myself or not locked?
		auto start = get_micros();
		bool result = b->sync();
		is->io_wait_cur += get_micros() - start;
		return result ? rw_ok : rw_fail_general;
	}

	return rw_fail_locked;
}

void scsi::get_and_reset_stats(uint64_t *const bytes_read, uint64_t *const bytes_written, uint64_t *const n_syncs, uint64_t *const n_trims)
{
	return b->get_and_reset_stats(bytes_read, bytes_written, n_syncs, n_trims);
}

scsi::scsi_rw_result scsi::write(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data)
{
	is->n_writes++;
	is->bytes_written += n_blocks * b->get_block_size();

	if (locking_status() != l_locked_other) {  // locked by myself or not locked?
		bool result = false;
		auto start = get_micros();
		if (trim_level == 2) {
			bool is_zero = true;
			auto bs = get_block_size();
			uint8_t *zero = new uint8_t[bs]();
			for(uint32_t i=0; i<n_blocks; i++) {
				if (memcmp(&data[i * bs], zero, bs) != 0) {
					is_zero = false;
					break;
				}
			}
			delete [] zero;

			if (is_zero)
				result = b->trim(block_nr, n_blocks);
			else
				result = b->write(block_nr, n_blocks, data);
		}
		else {
			result = b->write(block_nr, n_blocks, data);
		}

		is->io_wait_cur += get_micros() - start;
		return result ? rw_ok : rw_fail_general;
	}

	return rw_fail_locked;
}

scsi::scsi_rw_result scsi::trim(const uint64_t block_nr, const uint32_t n_blocks)
{
	if (locking_status() != l_locked_other) {  // locked by myself or not locked?
		auto start = get_micros();
		if (trim_level == 0) {  // 0 = do not trim/unmap
			scsi::scsi_rw_result rc = rw_ok;
			uint8_t *zero = new uint8_t[get_block_size()]();
			for(uint32_t i=0; i<n_blocks; i++) {
				rc = write(block_nr + i, 1, zero);
				if (rc != rw_ok)
					break;
			}
			delete [] zero;

			is->io_wait_cur += get_micros() - start;

			return rc;
		}
		else {
			bool result = b->trim(block_nr, n_blocks);
			is->io_wait_cur += get_micros() - start;
			if (result)
				return rw_ok;
		}

		return rw_fail_general;
	}

	return rw_fail_locked;
}

scsi::scsi_rw_result scsi::read(const uint64_t block_nr, const uint32_t n_blocks, uint8_t *const data)
{
	is->n_reads++;
	is->bytes_read += n_blocks * b->get_block_size();

	if (locking_status() != l_locked_other) {  // locked by myself or not locked?
		auto start = get_micros();
		bool result = b->read(block_nr, n_blocks, data);
		is->io_wait_cur += get_micros() - start;
		return result ? rw_ok : rw_fail_general;
	}
	
	return rw_fail_locked;
}

scsi::scsi_lock_status scsi::reserve_device()
{
#if !defined(TEENSY4_1)
	std::unique_lock lck(locked_by_lock);

	auto cur_id = std::this_thread::get_id();

	if (locked_by.has_value()) {
		if (locked_by.value() == cur_id)
			return l_locked;

		return l_locked_other;
	}

	locked_by = std::this_thread::get_id();
#endif
	return l_locked;
}

bool scsi::unlock_device()
{
#if defined(TEENSY4_1)
	return true;
#else
	std::unique_lock lck(locked_by_lock);

	if (locked_by.has_value() == false) {
		errlog("scsi::unlock_device: device was NOT locked!");
		return false;
	}

	if (locked_by.value() == std::this_thread::get_id()) {
		locked_by.reset();
		return true;
	}

	errlog("scsi::unlock_device: device is locked by someone else!");

	return false;
#endif
}

scsi::scsi_lock_status scsi::locking_status()
{
#if defined(TEENSY4_1)
	return l_not_locked;  // TODO
#else
	std::unique_lock lck(locked_by_lock);

	if (locked_by.has_value() == false)
		return l_not_locked;

	if (locked_by.value() == std::this_thread::get_id())
		return l_locked;

	return l_locked_other;
#endif
}

std::vector<uint8_t> scsi::error_reserve_6() const
{
	// sense key 0x05, asc 0x2c, ascq 0x09; 'illegal request':: 'PREVIOUS RESERVATION CONFLICT STATUS'
	return  { 0x70, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x2c, 0x09, 0x00, 0x00, 0x00, 0x00 };
	//                    ^^^^                                                        ^^^^  ^^^^
}

std::vector<uint8_t> scsi::error_not_implemented() const
{
	return { 0x70, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00 };
}

std::vector<uint8_t> scsi::error_write_error() const
{
	return { 0x70, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00 };
}

std::vector<uint8_t> scsi::error_compare_and_write_count() const
{
	// sense key 0x05, asc 0x24, ascq 0x00
	return { 0x70, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00 };
	//                   ^^^^                                                        ^^^^  ^^^^
}

std::vector<uint8_t> scsi::error_out_of_range() const
{
	// ILLEGAL_REQUEST(0x05)/LBA out of range(0x2100)
	return { 0x70, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00 };
}

std::vector<uint8_t> scsi::error_miscompare() const
{
	// Miscompare - the source data did not match the data read from the medium
	// sense key 0x0e, asc 0x1d, ascq 0x00
	return { 0x70, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x00 };
	//                   ^^^^                                                        ^^^^  ^^^^
}
