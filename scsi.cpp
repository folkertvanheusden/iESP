#if defined(TEENSY4_1)
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
	const char *name;
} scsi_opcode_details;

const std::map<scsi::scsi_opcode, scsi_opcode_details> scsi_a3_data {
	{ scsi::scsi_opcode::o_test_unit_ready, { { 0xff, 0x00, 0x00, 0x00, 0x00, 0x07 }, 6, "test unit ready" } },
	{ scsi::scsi_opcode::o_request_sense,   { { 0xff, 0x01, 0x00, 0x00, 0xff, 0x07 }, 6, "request sense"   } },
	{ scsi::scsi_opcode::o_read_6,		{ { 0xff, 0x1f, 0xff, 0xff, 0xff, 0x07 }, 6, "read 6"          } },
	{ scsi::scsi_opcode::o_write_6,		{ { 0xff, 0x1f, 0xff, 0xff, 0xff, 0x07 }, 6, "write 6"         } },
//	{ scsi::scsi_opcode::o_seek,		{
	{ scsi::scsi_opcode::o_inquiry,		{ { 0xff, 0x01, 0xff, 0xff, 0xff, 0x07 }, 6, "inquiry"         } },
#if !defined(TEENSY4_1) && !defined(RP2040W)
	{ scsi::scsi_opcode::o_reserve_6,	{ { 0xff, 0x00, 0x00, 0x00, 0x00, 0x07 }, 6, "reserve 6"       } },
	{ scsi::scsi_opcode::o_release_6,	{ { 0xff, 0x00, 0x00, 0x00, 0x00, 0x07 }, 6, "release 6"       } },
#endif
	{ scsi::scsi_opcode::o_mode_sense_6,	{ { 0xff, 0x08, 0xff, 0xff, 0xff, 0x07 }, 6, "mode sense 6"    } },
	{ scsi::scsi_opcode::o_read_capacity_10,{ { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07 }, 10, "read capacity 10" } },
	{ scsi::scsi_opcode::o_read_10,		{ { 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0x07 }, 10, "read 10"          } },
	{ scsi::scsi_opcode::o_write_10,	{ { 0xff, 0xfa, 0xff, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0x07 }, 10, "write 10"         } },
	{ scsi::scsi_opcode::o_write_same_10,	{ { 0xff, 0x08, 0xff, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff }, 10, "write same 10"    } },
	{ scsi::scsi_opcode::o_write_same_16,	{ { 0xff, 0x08, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xff }, 16, "write same 16" } },
	{ scsi::scsi_opcode::o_write_verify_10,	{ { 0xff, 0xf2, 0xff, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0x07 }, 10, "write verify 10" } },
	{ scsi::scsi_opcode::o_sync_cache_10,	{ { 0xff, 0x06, 0xff, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0x07 }, 10, "sync cache 10"   } },
	{ scsi::scsi_opcode::o_unmap,           { { 0xff, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x07 }, 10, "unmap"           } },
	{ scsi::scsi_opcode::o_read_12,		{ { 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x07 }, 12, "read 12"  } },
	{ scsi::scsi_opcode::o_write_12,	{ { 0xff, 0xfa, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x07 }, 12, "write 12" } },
	{ scsi::scsi_opcode::o_read_16,		{ { 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x07 }, 16, "read 16" } },
	{ scsi::scsi_opcode::o_compare_and_write, { { 0xff, 0xfa, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00, 0x07 }, 16, "compare and write" } },
	{ scsi::scsi_opcode::o_write_16,	{ { 0x8a, 0xfa, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x07 }, 16, "write 16" } },
	{ scsi::scsi_opcode::o_get_lba_status,	{ { 0xff, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x07 }, 16, "get lba status" } },
	{ scsi::scsi_opcode::o_report_luns,	{ { 0xff, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x07 }, 12, "report luns" } },
	{ scsi::scsi_opcode::o_rep_sup_oper,	{ { 0xff, 0x1f, 0x87, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x07 }, 12, "report supported operations" } },
};

#if defined(ARDUINO)
#define MAX_WS_LEN 2
#else
#define MAX_WS_LEN 256
#endif

constexpr const uint8_t max_compare_and_write_block_count = 1;

scsi::scsi(backend *const b, const int trim_level) : b(b), trim_level(trim_level), serial(b->get_serial())
{
}

scsi::~scsi()
{
#if !defined(ARDUINO) && !defined(NDEBUG)
	DOLOG(logging::ll_info, "~scsi()", "-", "SCSI opcode usage counts:");
	for(auto & e: scsi_a3_data)
		DOLOG(logging::ll_info, "~scsi()", "-", "  %02x: %" PRIu64 " (%s)", e.first, cmd_use_count[e.first].load(), e.second.name);
#endif
}

scsi_response scsi::test_unit_ready(const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data)
{
	scsi_response response(ir_empty_sense);

	DOLOG(logging::ll_debug, "scsi::test_unit_ready", identifier, "TEST UNIT READY");

	return response;
}

scsi_response scsi::mode_sense_6(const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data)
{
	scsi_response response(ir_as_is);

	if (locking_status() == l_locked_other) {
		DOLOG(logging::ll_error, "scsi::mode_sense_6", identifier, "MODE SENSE 6 failed due to reservations");
		response.sense_data = error_reservation_conflict_1();
	}
	else {
		DOLOG(logging::ll_debug, "scsi::mode_sense_6", identifier, "MODE SENSE 6");
		if (CDB[1] & 8)
			DOLOG(logging::ll_debug, "scsi::mode_sense_6", identifier, "MODE SENSE 6: DBD");
		uint8_t page_control = CDB[2] >> 6;
		const char *const pagecodes[] { "current values", "changeable values", "default values", "saved values " };
		DOLOG(logging::ll_debug, "scsi::mode_sense_6", identifier, "MODE SENSE 6: PAGE CONTROL %s (%d)", pagecodes[page_control], page_control);
		uint8_t page_code = CDB[2] & 0x3f;
		DOLOG(logging::ll_debug, "scsi::mode_sense_6", identifier, "MODE SENSE 6: PAGE CODE %02xh", page_code);
		DOLOG(logging::ll_debug, "scsi::mode_sense_6", identifier, "MODE SENSE 6: SUBPAGE CODE %02xh", CDB[3]);
		DOLOG(logging::ll_debug, "scsi::mode_sense_6", identifier, "MODE SENSE 6: AllocationLength: %d", CDB[4]);
		DOLOG(logging::ll_debug, "scsi::mode_sense_6", identifier, "MODE SENSE 6: Control: %02xh", CDB[5]);
		response.io.is_inline          = true;
		response.io.what.data.second   = 4;
		response.io.what.data.first    = new uint8_t[response.io.what.data.second]();
		response.io.what.data.first[0] = response.io.what.data.second - 1;  // length
		response.io.what.data.first[1] = 0;  // medium type
		response.io.what.data.first[2] = 16;  // DPOFUA
	}

	return response;
}

scsi_response scsi::inquiry(const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data)
{
	scsi_response response(ir_as_is);

	DOLOG(logging::ll_debug, "scsi::inquiry", identifier, "INQUIRY");
	if (CDB[1] & 1) {
		DOLOG(logging::ll_debug, "scsi::inquiry", identifier, "INQUIRY: EVPD");
		DOLOG(logging::ll_debug, "scsi::inquirinquiry", identifier, "INQUIRY: PageCode: %02xh", CDB[2]);
	}
	if (CDB[1] & 2)
		DOLOG(logging::ll_debug, "scsi::inquiry", identifier, "INQUIRY: CmdDt");
	uint16_t allocation_length = (CDB[3] << 8) | CDB[4];
	DOLOG(logging::ll_debug, "scsi::inquiry", identifier, "INQUIRY: AllocationLength: %d", allocation_length);
	DOLOG(logging::ll_debug, "scsi::inquiry", identifier, "INQUIRY: ControlByte: %02xh", CDB[5]);
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
			memcpy(&response.io.what.data.first[32], VERSION, 3);
			memset(&response.io.what.data.first[36], '0', 8);
			memcpy(&response.io.what.data.first[36], serial.c_str(), std::min(serial.size(), size_t(8)));
			// https://www.t10.org/lists/stds-num.htm
			response.io.what.data.first[58] = 0x06;  // SBC-4
			response.io.what.data.first[59] = 0x00;
			response.io.what.data.first[60] = 0x09;  // iSCSI
			response.io.what.data.first[61] = 0x60;
			response.io.what.data.first[62] = 0x01;  // SCC-2
			response.io.what.data.first[63] = 0xfb;
			response.io.what.data.first[64] = 0x01;  // SPC
			response.io.what.data.first[65] = 0x20;
			response.io.what.data.first[66] = 0x04;  // SBC-3
			response.io.what.data.first[67] = 0xc0;
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
			response.io.what.data.second   = 64;
			response.io.what.data.first    = new uint8_t[response.io.what.data.second]();
			response.io.what.data.first[0] = device_type;
			response.io.what.data.first[1] = CDB[2];
			response.io.what.data.first[2] = 0;
			response.io.what.data.first[3] = 0x3c;  // page length
			response.io.what.data.first[4] = 0;  // WSNZ bit
			response.io.what.data.first[5] = max_compare_and_write_block_count;  // compare and write
			response.io.what.data.first[6] = 0;  // OPTIMAL TRANSFER LENGTH GRANULARITY
			response.io.what.data.first[7] = 0;
#if defined(ESP32)
			response.io.what.data.first[22] = 1;  // 'MAXIMUM UNMAP LBA COUNT': 256 blocks
#define MAX_UNMAP_BLOCKS 256
#else
			response.io.what.data.first[22] = 32;  // 'MAXIMUM UNMAP LBA COUNT': 8192 blocks
#define MAX_UNMAP_BLOCKS 8192
#endif
			response.io.what.data.first[23] = 00;  // LSB of 'MAXIMUM UNMAP LBA COUNT'
			response.io.what.data.first[27] = 8;  // LSB of 'MAXIMUM UNMAP BLOCK DESCRIPTOR COUNT'
			response.io.what.data.first[31] = 8;  // LSB of 'OPTIMAL UNMAP GRANULARITY'
			response.io.what.data.first[40] = uint8_t(MAX_WS_LEN >> 24);  // 'MAXIMUM WRITE SAME LENGTH'
			response.io.what.data.first[41] = uint8_t(MAX_WS_LEN >> 16);
			response.io.what.data.first[42] = uint8_t(MAX_WS_LEN >> 8);
			response.io.what.data.first[43] = uint8_t(MAX_WS_LEN);
			// ... set rest to 'not set'
		}
		else if (CDB[2] == 0xb1) {  // block device characteristics
			response.io.is_inline          = true;
			response.io.what.data.second   = 64;
			response.io.what.data.first    = new uint8_t[response.io.what.data.second]();
			response.io.what.data.first[0] = device_type;
			response.io.what.data.first[1] = CDB[2];
			response.io.what.data.first[2] = (response.io.what.data.second - 4)>> 8;  // page length
			response.io.what.data.first[3] = response.io.what.data.second - 4;
			response.io.what.data.first[4] = 0x1c;  // device has an RPM of 7200 (fake!)
			response.io.what.data.first[5] = 0x20;
			response.io.what.data.first[8] = 0x02;  // FUAB
			// ... set all to 'not set'
		}
		else if (CDB[2] == 0xb2) {  // logical block provisioning vpd page
			response.io.is_inline          = true;
			response.io.what.data.second   = 64;
			response.io.what.data.first    = new uint8_t[response.io.what.data.second]();
			response.io.what.data.first[0] = device_type;
			response.io.what.data.first[1] = CDB[2];
			response.io.what.data.first[2] = (response.io.what.data.second - 4)>> 8;  // page length
			response.io.what.data.first[3] = response.io.what.data.second - 4;
			response.io.what.data.first[5] = 128 /* LBPU */ | 64 /* LBPWS */ | 32 /* LBPWS10 */ | 2 /* LBRZ: zeros */;
			// TODO
		}
		else {
			DOLOG(logging::ll_warning, "scsi::inquiry", identifier, "INQUIRY page code %02xh not implemented", CDB[2]);
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

	return response;
}

scsi_response scsi::read_capacity_10(const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data)
{
	scsi_response response(ir_as_is);

	DOLOG(logging::ll_debug, "scsi::read_capacity_10", identifier, "READ_CAPACITY");
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

	return response;
}

scsi_response scsi::get_lba_status(const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data)
{
	scsi_response response(ir_as_is);

	uint8_t service_action = CDB[1] & 31;
	DOLOG(logging::ll_debug, "scsi::get_lba_status", identifier, "ServiceAction: %02xh", service_action);

	if (service_action == 0x10) {  // READ CAPACITY
		DOLOG(logging::ll_debug, "scsi::get_lba_status", identifier, "READ_CAPACITY(16)");

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
		DOLOG(logging::ll_debug, "scsi::get_lba_status", identifier, "GET_LBA_STATUS");

		uint64_t lba             = get_uint64_t(&CDB[2]);

		auto vr = validate_request(lba);
		if (vr.has_value()) {
			DOLOG(logging::ll_debug, "scsi::get_lba_status", identifier, "GET LBA STATUS parameters invalid");
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
		DOLOG(logging::ll_warning, "scsi::get_lba_status", identifier, "GET LBA STATUS service action %02xh not implemented", service_action);
		response.sense_data = error_not_implemented();
	}

	return response;
}

void get_lba_len(const uint8_t *const CDB, const cdb_size_t s, uint64_t *const lba, uint32_t *const transfer_length)
{
	if (s == cs_6) {
		*lba             = ((CDB[1] & 31) << 16) | (CDB[2] << 8) | CDB[3];
		*transfer_length = CDB[4];
		if (*transfer_length == 0)
			*transfer_length = 256;
	}
	else if (s == cs_10) {
		*lba             = get_uint32_t(&CDB[2]);
		*transfer_length =  (CDB[7] << 8) | CDB[8];
	}
	else if (s == cs_12) {
		*lba             = get_uint32_t(&CDB[2]);
		*transfer_length = get_uint32_t(&CDB[6]);
	}
	else if (s == cs_16) {
		*lba             = get_uint64_t(&CDB[2]);
		*transfer_length = get_uint32_t(&CDB[10]);
	}
	else {
		*lba = 0;
		*transfer_length = 0;

		DOLOG(logging::ll_error, "get_lba_len", "-", "unsupported LBA-length parameter %d", s);
		assert(false);  // should not be triggered, would be missing code
	}
}

scsi_response scsi::write_verify(io_stats_t *const is, const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data, const uint8_t opcode)
{
	scsi_response response(ir_as_is);

	uint64_t    lba             = 0;
	uint32_t    transfer_length = 0;
	const char *name            = "?";

	if (opcode == o_write_6) {
		get_lba_len(CDB, cs_6, &lba, &transfer_length);
		name = "6";
	}
	else if (opcode == o_write_10 || opcode == o_write_verify_10) {
		// NOTE: the verify part is not implemented, o_write_verify_10 is just a dumb write
		get_lba_len(CDB, cs_10, &lba, &transfer_length);
		name = opcode == o_write_verify_10 ? "verify-10" : "10";
	}
	else if (opcode == o_write_12) {
		get_lba_len(CDB, cs_12, &lba, &transfer_length);
		name = "12";
	}
	else if (opcode == o_write_16) {
		get_lba_len(CDB, cs_16, &lba, &transfer_length);
		name = "16";
	}
	else {
		DOLOG(logging::ll_error, "scsi::write_", identifier, "Unknown write mode");
	}

	bool fua = CDB[1] & 8;

	auto backend_block_size = b->get_block_size();
	response.amount_of_data_expected = transfer_length * backend_block_size;

	DOLOG(logging::ll_debug, "scsi::write_verify", identifier, "WRITE_%s, offset %" PRIu64 ", %u sectors", name, lba, transfer_length);

	auto vr = validate_request(lba, transfer_length, CDB);
	if (vr.has_value()) {
		DOLOG(logging::ll_debug, "scsi::write_verify", identifier, "WRITE_1x parameters invalid");
		response.sense_data = vr.value();
	}
	else if (data.first) {
		DOLOG(logging::ll_debug, "scsi::write_verify", identifier, "write command includes data (%zu bytes)", data.second);

		size_t expected_size      = transfer_length * backend_block_size;
		size_t received_size      = data.second;
		size_t received_blocks    = received_size / backend_block_size;
		if (received_blocks)
			DOLOG(logging::ll_debug, "scsi::write_verify", identifier, "WRITE_xx to LBA %" PRIu64 " is %zu in bytes, %zu bytes", lba, lba * backend_block_size, received_size);

		bool                 ok = true;
		scsi::scsi_rw_result rc = scsi_rw_result::rw_ok;

		uint32_t work_n_blocks  = std::min(transfer_length, uint32_t(received_blocks));
		if (received_blocks > 0) {
			rc = write(is, lba, work_n_blocks, data.first);
			ok = rc == scsi_rw_result::rw_ok;
		}

		size_t fragment_size = received_size - work_n_blocks * backend_block_size;
		if (ok && fragment_size > 0 && fragment_size < backend_block_size) {
			uint8_t *temp_buffer = new uint8_t[backend_block_size];

			// received_blocks is rounded down above
			rc = read(is, lba + work_n_blocks, 1, temp_buffer);
			if (rc == scsi_rw_result::rw_ok) {
				memcpy(temp_buffer, &data.first[work_n_blocks * backend_block_size], fragment_size);
				rc = write(is, lba + received_blocks, 1, temp_buffer);
			}

			ok = rc == scsi_rw_result::rw_ok;

			delete [] temp_buffer;
		}

		if (!ok) {
			if (rc == scsi_rw_result::rw_fail_rw) {
				DOLOG(logging::ll_error, "scsi::write_verify", identifier, "WRITE_xx, general i/o error");
				response.sense_data = error_write_error();
			}
			else if (rc == rw_fail_locked) {
				DOLOG(logging::ll_error, "scsi::write_verify", identifier, "WRITE_xx, failed i/o due to reservations");
				response.sense_data = error_reservation_conflict_1();
			}
		}

		if (ok) {
			if (received_size == expected_size) {
				response.type = ir_empty_sense;
				DOLOG(logging::ll_debug, "scsi::write_verify", identifier, "received_size == expected_size");

				if (fua)
					this->sync(is);
			}
			else {  // allow R2T packets to come in
				response.type           = ir_r2t;
				response.r2t.buffer_lba = lba;
				response.r2t.fua        = fua;
				if (received_blocks <= transfer_length)
					response.r2t.bytes_left = (transfer_length - received_blocks) * backend_block_size;
				else
					response.r2t.bytes_left = 0;
				response.r2t.bytes_done      = received_blocks * backend_block_size;
				DOLOG(logging::ll_debug, "scsi::write_verify", identifier, "starting R2T with %u bytes left (LBA: %" PRIu64 ", offset %u)", response.r2t.bytes_left, response.r2t.buffer_lba, response.r2t.bytes_done);
			}
		}
	}
	else {
		DOLOG(logging::ll_debug, "scsi::write_verify", identifier, "WRITE without data");

		if (transfer_length) {
			response.type           = ir_r2t;  // allow R2T packets to come in
			response.r2t.buffer_lba = lba;
			response.r2t.fua        = fua;
			response.r2t.bytes_left = transfer_length * backend_block_size;
			DOLOG(logging::ll_debug, "scsi::write_verify", identifier, "starting R2T with %u bytes left (LBA: %" PRIu64 ")", response.r2t.bytes_left, response.r2t.buffer_lba);
		}
		else {
			response.type = ir_empty_sense;
			DOLOG(logging::ll_debug, "scsi::write_verify", identifier, "WRITE with 0 transfer_length");
		}
	}

	return response;
}

scsi_response scsi::read_(const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data, const uint8_t opcode)
{
	scsi_response response(ir_as_is);

	uint64_t lba             = 0;
	uint32_t transfer_length = 0;

	if (opcode == o_read_16) {
		get_lba_len(CDB, cs_16, &lba, &transfer_length);
		DOLOG(logging::ll_debug, "scsi::read_", identifier, "READ_16, LBA %" PRIu64 ", %u sectors", lba, transfer_length);
	}
	else if (opcode == o_read_12) {
		get_lba_len(CDB, cs_12, &lba, &transfer_length);
		DOLOG(logging::ll_debug, "scsi::read_", identifier, "READ_12, LBA %" PRIu64 ", %u sectors", lba, transfer_length);
	}
	else if (opcode == o_read_10) {
		get_lba_len(CDB, cs_10, &lba, &transfer_length);
		DOLOG(logging::ll_debug, "scsi::read_", identifier, "READ_10, LBA %" PRIu64 ", %u sectors", lba, transfer_length);
	}
	else if (opcode == o_read_6) {
		get_lba_len(CDB, cs_6, &lba, &transfer_length);
		DOLOG(logging::ll_debug, "scsi::read_", identifier, "READ_6, LBA %" PRIu64 ", %u sectors", lba, transfer_length);
	}
	else {
		DOLOG(logging::ll_error, "scsi::read_", identifier, "Unknown read mode");
	}

	auto backend_block_size = b->get_block_size();
	response.amount_of_data_expected = transfer_length * backend_block_size;

	auto vr = validate_request(lba, transfer_length, CDB);
	if (vr.has_value()) {
		DOLOG(logging::ll_debug, "scsi::read_", identifier, "READ_1x parameters invalid");
		response.sense_data = vr.value();
	}
	else if (transfer_length == 0) {
		DOLOG(logging::ll_debug, "scsi::read_", identifier, "READ_1x 0-read");
		response.type = ir_empty_sense;
	}
	else {
		response.io.is_inline               = false;
		response.io.what.location.lba       = lba;
		response.io.what.location.n_sectors = transfer_length;
		response.io.what.data.first         = nullptr;
		response.data_is_meta               = false;
	}

	return response;
}

scsi_response scsi::sync_cache(io_stats_t *const is, const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data)
{
	scsi_response response(ir_empty_sense);

	DOLOG(logging::ll_debug, "scsi::sync_cache", identifier, "SYNC CACHE 10");
	this->sync(is);

	return response;
}

scsi_response scsi::report_luns(const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data)
{
	scsi_response response(ir_as_is);

	DOLOG(logging::ll_debug, "scsi::report_luns", identifier, "REPORT_LUNS, report: %02xh", CDB[2]);

	response.io.is_inline           = true;
	response.io.what.data.second    = 16;
	response.io.what.data.first     = new uint8_t[response.io.what.data.second]();
	response.io.what.data.first[3]  = 8;  // lun list length
					      // 4...7 reserved
	encode_lun(&response.io.what.data.first[8], 1);  // LUN1

	return response;
}

scsi_response scsi::report_supported_operation_codes(const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data)
{
	scsi_response response(ir_as_is);

	uint8_t service_action     = CDB[1] & 31;
	uint8_t reporting_options  = CDB[2] & 7;
	uint8_t req_operation_code = CDB[3];
	DOLOG(logging::ll_debug, "scsi::report_supported_operation_codes", identifier, "REPORT SUPPORTED OPERATION CODES, service action %02xh, requested operation code %02xh, reporting options: %xh", service_action, req_operation_code, reporting_options);
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
				auto cdb_size = it->second.data.size();
				response.io.what.data.second    = 4 + cdb_size;
				response.io.what.data.first     = new uint8_t[response.io.what.data.second]();
				response.io.what.data.first[1]  = 0x03;  // SUPPORT == 0b011
				response.io.what.data.first[2]  = cdb_size >> 8;
				response.io.what.data.first[3]  = cdb_size;
				memcpy(response.io.what.data.first + 4, it->second.data.data(), cdb_size);

				ok = true;
			}
			else {
				DOLOG(logging::ll_warning, "scsi::report_supported_operation_codes", identifier, "0xa3 for opcode %02x not implemented", req_operation_code);
			}
		}
		else {
			DOLOG(logging::ll_warning, "scsi::report_supported_operation_codes", identifier, "0xa3 reporting option %d not implemented", reporting_options);
		}
	}
	else {
		DOLOG(logging::ll_warning, "scsi::report_supported_operation_codes", identifier, "0xa3 service action %02x not implemented", service_action);
	}

	if (!ok)
		response.sense_data = error_not_implemented();

	return response;
}

scsi_response scsi::compare_and_write(io_stats_t *const is, const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data)
{
	scsi_response response(ir_as_is);

	uint64_t lba         = get_uint64_t(&CDB[2]);
	uint32_t block_count = CDB[13];
	DOLOG(logging::ll_debug, "scsi::compare_and_write", identifier, "COMPARE AND WRITE: LBA %" PRIu64 ", transfer length: %u", lba, block_count);

	auto block_size         = b->get_block_size();
	auto expected_data_size = block_size * block_count * 2;
	if (expected_data_size != data.second)
		DOLOG(logging::ll_warning, "scsi::compare_and_write", identifier, "COMPARE AND WRITE: data count mismatch (%zu versus %zu)", size_t(expected_data_size), data.second);

	response.amount_of_data_expected = expected_data_size;

	auto vr = validate_request(lba, block_count, CDB);
	if (vr.has_value()) {
		DOLOG(logging::ll_debug, "scsi::compare_and_write", identifier, "COMPARE AND WRITE parameters invalid");
		response.sense_data = vr.value();
	}
	else if (block_count > max_compare_and_write_block_count) {
		DOLOG(logging::ll_debug, "scsi::compare_and_write", identifier, "COMPARE AND WRITE: too many blocks in one go (%u versus %u)", block_count, max_compare_and_write_block_count);
		response.sense_data = error_compare_and_write_count();
	}
	else {
		auto result = cmpwrite(is, lba, block_count, &data.first[block_count * block_size], &data.first[0]);

		if (result == scsi_rw_result::rw_ok)
			response.type = ir_empty_sense;
		else if (result == scsi_rw_result::rw_fail_locked)
			response.sense_data = error_reservation_conflict_1();
		else if (result == scsi_rw_result::rw_fail_mismatch)
			response.sense_data = error_miscompare();
		else if (result == scsi_rw_result::rw_fail_rw)
			response.sense_data = error_write_error();
		else {
			DOLOG(logging::ll_error, "scsi::compare_and_write", identifier, "unexpected error for COMPARE AND WRITE: %d", result);
		}
	}

	return response;
}

scsi_response scsi::prefetch(const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data, const uint8_t opcode)
{
	scsi_response response(ir_as_is);

	DOLOG(logging::ll_debug, "scsi::prefetch", identifier, "PREFETCH 10/16");

	uint64_t lba             = 0;
	uint32_t transfer_length = 0;

	if (opcode == o_prefetch_16) {
		get_lba_len(CDB, cs_16, &lba, &transfer_length);
		DOLOG(logging::ll_debug, "scsi::prefetch", identifier, "PREFETCH_16, LBA %" PRIu64 ", %u sectors", lba, transfer_length);
	}
	else if (opcode == o_prefetch_10) {
		get_lba_len(CDB, cs_10, &lba, &transfer_length);
		DOLOG(logging::ll_debug, "scsi::prefetch", identifier, "PREFETCH_10, LBA %" PRIu64 ", %u sectors", lba, transfer_length);
	}

	auto vr = validate_request(lba, transfer_length, nullptr);
	if (vr.has_value()) {
		DOLOG(logging::ll_debug, "scsi::prefetch", identifier, "PREFETCH parameters invalid");
		response.sense_data = vr.value();
	}
	else {
		response.type = ir_empty_sense;
	}

	return response;
}

scsi_response scsi::reserve(const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data)
{
	scsi_response response(ir_as_is);

	DOLOG(logging::ll_debug, "scsi::reserve", identifier, "RESERVE 6");

#if !defined(TEENSY4_1) && !defined(RP2040W)
	if (reserve_device() == l_locked)
		response.type = ir_empty_sense;
	else {
		DOLOG(logging::ll_debug, "scsi::reserve", identifier, "RESERVE 6 failed");
		response.sense_data = error_reservation_conflict_1();
	}
#else
	response.sense_data = error_not_implemented();
#endif

	return response;
}

scsi_response scsi::release(const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data)
{
	scsi_response response(ir_as_is);

	DOLOG(logging::ll_debug, "scsi::release", identifier, "RELEASE 6");

#if !defined(TEENSY4_1) && !defined(RP2040W)
	unlock_device();
	response.type         = ir_empty_sense;
#else
	response.sense_data   = error_not_implemented();
#endif

	return response;
}

scsi_response scsi::unmap(io_stats_t *const is, const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data)
{
	scsi_response response(ir_as_is);

	DOLOG(logging::ll_debug, "scsi::unmap", identifier, "UNMAP");

	const uint8_t *const pd = data.first;
	scsi_rw_result rc = rw_ok;
	for(size_t i=8; i<data.second; i+= 16) {
		uint64_t lba             = get_uint64_t(&pd[i]);
		uint32_t transfer_length = get_uint32_t(&pd[i + 8]);

		auto vr = validate_request(lba, transfer_length, nullptr);
		if (vr.has_value() || transfer_length > MAX_UNMAP_BLOCKS) {
			if (vr.has_value()) {
				DOLOG(logging::ll_debug, "scsi::unmap", identifier,"UNMAP parameters invalid");
				response.sense_data = vr.value();
			}
			else {
				DOLOG(logging::ll_debug, "scsi::unmap", identifier,"UNMAP parameters out of range");
				response.sense_data = error_out_of_range();
			}
			rc = rw_fail_general;
		}
		else if (transfer_length > 0) {  // 0 is not an error but the backend may not handle it well
			DOLOG(logging::ll_debug, "scsi::unmap", identifier, "UNMAP trim LBA %" PRIu64 ", %u blocks", lba, transfer_length);

			rc = trim(is, lba, transfer_length);
			if (rc != rw_ok) {
				DOLOG(logging::ll_error, "scsi::unmap", identifier, "UNMAP trim failed");
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
		response.sense_data = error_reservation_conflict_1();
	else {
		response.sense_data = error_write_error();
	}

	return response;
}

scsi_response scsi::write_same(io_stats_t *const is, const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data, const uint8_t opcode)
{
	scsi_response response(ir_as_is);

	DOLOG(logging::ll_debug, "scsi::write_same", identifier, "WRITE SAME 10/16");

	uint64_t lba             = 0;
	uint32_t transfer_length = 0;

	response.r2t.write_same_is_unmap = CDB[1] & 8;

	if (opcode == o_write_same_16) {
		get_lba_len(CDB, cs_16, &lba, &transfer_length);
		DOLOG(logging::ll_debug, "scsi::write_same", identifier, "WRITE_SAME_16, LBA %" PRIu64 ", %u sectors, is unmap: %d", lba, transfer_length, response.r2t.write_same_is_unmap);
	}
	else if (opcode == o_write_same_10) {
		get_lba_len(CDB, cs_10, &lba, &transfer_length);
		DOLOG(logging::ll_debug, "scsi::write_same", identifier, "WRITE_SAME_10, LBA %" PRIu64 ", %u sectors, is unmap: %d", lba, transfer_length, response.r2t.write_same_is_unmap);
	}

	auto backend_block_size = b->get_block_size();

	response.r2t.is_write_same       = true;
	response.amount_of_data_expected = transfer_length * backend_block_size;

	size_t expected_size = backend_block_size;

	auto vr = validate_request(lba, transfer_length, CDB);
	if (vr.has_value()) {
		DOLOG(logging::ll_debug, "scsi::write_same", identifier, "WRITE_SAME parameters invalid");
		response.sense_data = vr.value();
	}
	else if (data.first) {
		DOLOG(logging::ll_debug, "scsi::write_same", identifier, "WRITE SAME command includes data (%zu bytes)", data.second);

		size_t received_size   = data.second;
		size_t received_blocks = received_size / backend_block_size;

		bool ok = true;

		if (received_blocks == 1)
			DOLOG(logging::ll_debug, "scsi::write_same", identifier, "WRITE_SAME to LBA %" PRIu64 " is %zu in bytes", lba, backend_block_size);
		else {
			DOLOG(logging::ll_info, "scsi::write_same", identifier, "WRITE_SAME received block count (%u) != 1", received_blocks);
			ok = false;
		}

		if (ok) {
			scsi::scsi_rw_result rc = rw_ok;

			const uint64_t size_in_blocks = b->get_size_in_blocks();

			if (transfer_length == 0) {
				if (size_in_blocks - lba > MAX_WS_LEN) {
					DOLOG(logging::ll_debug, "scsi::validate_request", "-", "WRITE_SAME maximum number of blocks for TL=0");
					response.sense_data = error_invalid_field();
					ok = false;
				}
				else {
					for(uint64_t i=lba; i<b->get_size_in_blocks() && rc == rw_ok; i++) {
						rc = response.r2t.write_same_is_unmap ?
							trim(is, i, 1) :
							write(is, i, 1, data.first);
					}
				}
			}
			else {
				for(uint32_t i=0; i<transfer_length && rc == rw_ok; i++, lba++) {
					rc = response.r2t.write_same_is_unmap ?
						trim(is, lba, 1) :
						write(is, lba, 1, data.first);
				}
			}

			if (rc == scsi_rw_result::rw_fail_rw) {
				DOLOG(logging::ll_error, "scsi::write_same", identifier, "WRITE_SAME, general %s error", response.r2t.write_same_is_unmap ? "trim" : "write");
				response.sense_data = error_write_error();
				ok = false;
			}
			else if (rc == rw_fail_locked) {
				DOLOG(logging::ll_error, "scsi::write_same", identifier, "WRITE_SAME, failed due to reservations");
				response.sense_data = error_reservation_conflict_1();
				ok = false;
			}
		}

		if (ok) {
			response.type = ir_empty_sense;
			DOLOG(logging::ll_debug, "scsi::write_same", identifier, "received_size == expected_size");
		}
	}
	else {
		DOLOG(logging::ll_debug, "scsi::write_same", identifier, "WRITE_SAME without data");

		if (transfer_length == expected_size)
			response.type = ir_r2t;  // allow R2T packets to come in
		else {
			// TODO: error of transfer_length != 0?
			response.type = ir_empty_sense;
			DOLOG(logging::ll_debug, "scsi::write_same", identifier, "WRITE_SAME with 0 transfer_length");
		}
	}

	return response;
}

std::optional<scsi_response> scsi::send(io_stats_t *const is, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data)
{
	assert(size >= 16);

	scsi_opcode opcode = scsi_opcode(CDB[0]);
#if !defined(ARDUINO) && !defined(NDEBUG)
	cmd_use_count[CDB[0]]++;
#endif

	std::string lun_identifier = myformat("LUN:%" PRIu64, lun);
	DOLOG(logging::ll_debug, "scsi::send", lun_identifier, "SCSI opcode: %02xh, CDB size: %zu", opcode, size);
	DOLOG(logging::ll_debug, "scsi::send", lun_identifier, "CDB contents: %s", to_hex(CDB, size).c_str());

	scsi_response response(ir_as_is);

	if (opcode == o_test_unit_ready)
		response = test_unit_ready(lun_identifier, lun, CDB, size, data);
	else if (opcode == o_mode_sense_6)  // 0x1a
		response = mode_sense_6(lun_identifier, lun, CDB, size, data);
	else if (opcode == o_inquiry)  // 0x12
		response = inquiry(lun_identifier, lun, CDB, size, data);
	else if (opcode == o_read_capacity_10)
		response = read_capacity_10(lun_identifier, lun, CDB, size, data);
	else if (opcode == o_get_lba_status)
		response = get_lba_status(lun_identifier, lun, CDB, size, data);
	else if (opcode == o_write_6 || opcode == o_write_10 || opcode == o_write_verify_10 || opcode == o_write_12 || opcode == o_write_16)
		response = write_verify(is, lun_identifier, lun, CDB, size, data, opcode);
	else if (opcode == o_read_16 || opcode == o_read_12 || opcode == o_read_10 || opcode == o_read_6)  // 0x88, 0xa8, 0x28, 0x08
		response = read_(lun_identifier, lun, CDB, size, data, opcode);
	else if (opcode == o_sync_cache_10)  // 0x35
		response = sync_cache(is, lun_identifier, lun, CDB, size, data);
	else if (opcode == o_report_luns)  // 0xa0
		response = report_luns(lun_identifier, lun, CDB, size, data);
	else if (opcode == o_rep_sup_oper)  // 0xa3
		response = report_supported_operation_codes(lun_identifier, lun, CDB, size, data);
	else if (opcode == o_compare_and_write)  // 0x89
		response = compare_and_write(is, lun_identifier, lun, CDB, size, data);
	else if (opcode == o_prefetch_10 || opcode == o_prefetch_16)  // 0x34 & 0x90
		response = prefetch(lun_identifier, lun, CDB, size, data, opcode);
	else if (opcode == o_reserve_6)
		response = reserve(lun_identifier, lun, CDB, size, data);
	else if (opcode == o_release_6)
		response = release(lun_identifier, lun, CDB, size, data);
	else if (opcode == o_unmap)
		response = unmap(is, lun_identifier, lun, CDB, size, data);
	else if (opcode == o_write_same_10 || opcode == o_write_same_16)  // 0x41 & 0x93
		response = write_same(is, lun_identifier, lun, CDB, size, data, opcode);
	else {
		DOLOG(logging::ll_warning, "scsi::send", lun_identifier, "opcode %02xh not implemented", opcode);
		response.sense_data = error_not_implemented();
	}

	DOLOG(logging::ll_debug, "scsi::send", lun_identifier, "returning %zu bytes of sense data", response.sense_data.size());

	return response;
}

// returns sense data in case of a problem
std::optional<std::vector<uint8_t> > scsi::validate_request(const uint64_t lba, const uint32_t n_blocks, const uint8_t *const CDB) const
{
	auto size_in_blocks = get_size_in_blocks();

	if (lba > size_in_blocks - n_blocks) {
		DOLOG(logging::ll_debug, "scsi::validate_request", "-", "lba %" PRIu64 " + n_blocks %u > size_in_blocks %" PRIu64, lba, n_blocks, size_in_blocks);
		return error_out_of_range();
	}

	if (lba + n_blocks < lba) {
		DOLOG(logging::ll_debug, "scsi::validate_request", "-", "lba %" PRIu64 " + n_blocks %u wraps" PRIu64, lba, n_blocks);
		return error_out_of_range();
	}

	if (CDB) {
		scsi_opcode opcode = scsi_opcode(CDB[0]);

		if (opcode == o_read_10 || opcode == o_read_16) {
			if (CDB[1] >> 5) {  // RDPROTECT / WRPROTECT
				DOLOG(logging::ll_debug, "scsi::validate_request", "-", "RD/WR PROTECT not supported");
				return error_invalid_field();
			}
		}

		if ((opcode == o_write_same_10 || opcode == o_write_same_16) && (CDB[1] & 16) == 16 && (CDB[1] & 8) == 0) {
			DOLOG(logging::ll_debug, "scsi::validate_request", "-", "WRITE_SAME with ANCHOR=1 and UNMAP=0 is a failure");
			return error_invalid_field();
		}

		if ((opcode == o_write_same_10 || opcode == o_write_same_16) && n_blocks > MAX_WS_LEN) {
			DOLOG(logging::ll_debug, "scsi::validate_request", "-", "WRITE_SAME maximum number of blocks");
			return error_invalid_field();
		}

		if ((opcode == o_write_10 || opcode == o_write_16 || opcode == o_write_verify_10 || opcode == o_write_same_10 || opcode == o_write_same_16) && (CDB[1] >> 5) != 0) {
			DOLOG(logging::ll_debug, "scsi::validate_request", "-", "WRITE_SAME with WRPROTECT set is not supported");
			return error_invalid_field();
		}
	}

	return { };  // no error
}

std::optional<std::vector<uint8_t> > scsi::validate_request(const uint64_t lba) const
{
	auto size_in_blocks = get_size_in_blocks();

	if (lba >= size_in_blocks) {
		DOLOG(logging::ll_debug, "scsi::validate_request", "-", "lba %" PRIu64 " >= size_in_blocks %" PRIu64, lba, size_in_blocks);
		return error_out_of_range();
	}

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

scsi::scsi_rw_result scsi::sync(io_stats_t *const is)
{
	if (locking_status() != l_locked_other) {  // locked by myself or not locked?
		auto start   = get_micros();
		bool result  = b->sync();
		is->n_syncs++;
		is->io_wait += get_micros() - start;
		return result ? rw_ok : rw_fail_general;
	}

	return rw_fail_locked;
}

scsi::scsi_rw_result scsi::write(io_stats_t *const is, const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data)
{
	is->n_writes++;
	is->bytes_written += n_blocks * b->get_block_size();

	if (locking_status() != l_locked_other) {  // locked by myself or not locked?
		bool result = false;
		auto start = get_micros();
		if (trim_level == 2) {
			bool     is_zero = true;
			auto     bs      = get_block_size();
			uint8_t *zero    = new uint8_t[bs]();
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

		is->io_wait += get_micros() - start;

		return result ? rw_ok : rw_fail_general;
	}

	return rw_fail_locked;
}

scsi::scsi_rw_result scsi::trim(io_stats_t *const is, const uint64_t block_nr, const uint32_t n_blocks)
{
	if (locking_status() != l_locked_other) {  // locked by myself or not locked?
		is->blocks_trimmed += n_blocks;

		auto start = get_micros();
		if (trim_level == 0) {  // 0 = do not trim/unmap
			scsi::scsi_rw_result rc   = rw_ok;
			uint8_t             *zero = new uint8_t[get_block_size()]();
			for(uint32_t i=0; i<n_blocks; i++) {
				rc = write(is, block_nr + i, 1, zero);
				if (rc != rw_ok)
					break;
			}
			delete [] zero;

			is->io_wait += get_micros() - start;

			return rc;
		}
		else {
			bool result  = b->trim(block_nr, n_blocks);
			is->io_wait += get_micros() - start;
			if (result)
				return rw_ok;
		}

		return rw_fail_general;
	}

	return rw_fail_locked;
}

scsi::scsi_rw_result scsi::read(io_stats_t *const is, const uint64_t block_nr, const uint32_t n_blocks, uint8_t *const data)
{
	is->n_reads++;
	is->bytes_read += n_blocks * b->get_block_size();

	if (locking_status() != l_locked_other) {  // locked by myself or not locked?
		auto start   = get_micros();
		bool result  = b->read(block_nr, n_blocks, data);
		is->io_wait += get_micros() - start;
		return result ? rw_ok : rw_fail_general;
	}
	
	return rw_fail_locked;
}

scsi::scsi_rw_result scsi::cmpwrite(io_stats_t *const is, const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const write_data, const uint8_t *const compare_data)
{
	is->n_reads++;
	is->n_writes++;
	is->bytes_read    += n_blocks * b->get_block_size();
	is->bytes_written += n_blocks * b->get_block_size();

	if (locking_status() != l_locked_other) {
		auto start   = get_micros();
		auto result  = b->cmpwrite(block_nr, n_blocks, write_data, compare_data);

		is->io_wait += get_micros() - start;

		if (result == backend::cmpwrite_result_t::CWR_OK)
			return rw_ok;
		if (result == backend::cmpwrite_result_t::CWR_MISMATCH)
			return rw_fail_mismatch;
		if (result == backend::cmpwrite_result_t::CWR_READ_ERROR || result == backend::cmpwrite_result_t::CWR_WRITE_ERROR)
			return rw_fail_rw;

		return rw_fail_general;
	}

	return rw_fail_locked;
}

scsi::scsi_lock_status scsi::reserve_device()
{
#if !defined(TEENSY4_1) && !defined(RP2040W)
	std::unique_lock lck(locked_by_lock);

	auto cur_id = std::this_thread::get_id();

	if (locked_by.has_value()) {
		if (locked_by.value() == cur_id)
			return l_locked;

		return l_locked_other;
	}

	locked_by = cur_id;
#endif
	return l_locked;
}

bool scsi::unlock_device()
{
#if defined(TEENSY4_1) || defined(RP2040W)
	return true;
#else
	std::unique_lock lck(locked_by_lock);

	if (locked_by.has_value() == false) {
		DOLOG(logging::ll_error, "scsi::unlock_device", "-", "device was NOT locked!");
		return false;
	}

	if (locked_by.value() == std::this_thread::get_id()) {
		locked_by.reset();
		return true;
	}

	DOLOG(logging::ll_error, "scsi::unlock_device", "-", "device is locked by someone else!");

	return false;
#endif
}

scsi::scsi_lock_status scsi::locking_status()
{
#if defined(TEENSY4_1) || defined(RP2040W)
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

std::vector<uint8_t> scsi::error_reservation_conflict_1() const  // TODO naming
{
	// https://www.stix.id.au/wiki/SCSI_Sense_Data
	// sense key 0x06, asc 0x29, ascq 0x00; bus reset
	// 0x06: unit attention, 
	return  { 0x70, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00 };
	//                    ^^^^                                                        ^^^^  ^^^^
}

std::vector<uint8_t> scsi::error_reservation_conflict_2() const  // TODO naming
{
        // sense key 0x05, asc 0x2c, ascq 0x09; 'illegal request':: 'PREVIOUS RESERVATION CONFLICT STATUS'
        return  { 0x70, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x2c, 0x09, 0x00, 0x00, 0x00, 0x00 };
}

std::vector<uint8_t> scsi::error_not_implemented() const
{
	return { 0x70, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00 };
}

std::vector<uint8_t> scsi::error_write_error() const
{
	return { 0x70, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00 };
}

std::vector<uint8_t> scsi::error_read_error() const
{
	return { 0x70, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00 };
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

std::vector<uint8_t> scsi::error_invalid_field() const
{
	// ILLEGAL_REQUEST(0x05)/INVALID FIELD(0x2400)
	return { 0x70, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00 };
}
