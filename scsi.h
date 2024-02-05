#pragma once
#include <cstddef>
#include <cstdint>
#include <utility>

#include "backend.h"
#include "gen.h"
#include "iscsi.h"
#include "iscsi-pdu.h"


struct scsi_response
{
	iscsi_reacion_t              type;
	std::vector<uint8_t>         sense_data;  // error data
	bool                         data_is_meta;  // scsi command reply data

	struct {
		bool is_inline;  // if true, then next is valid

		struct {
			std::pair<uint8_t *, size_t> data;  // meta data or disk-data

			// if not inline
			data_descriptor location;
		} what;
	} io;

	r2t_session r2t;
};

class scsi
{
private:
	backend *const b         { nullptr };
	char           serial[9] { 0       };

public:
	scsi(backend *const b);
	virtual ~scsi();

	enum scsi_opcode {
		o_test_unit_ready  = 0x00,
		o_request_sense    = 0x02,
		o_read_6           = 0x08,
		o_write_6          = 0x0a,
		o_seek             = 0x0b,
		o_inquiry          = 0x12,
		o_mode_sense_6     = 0x1a,
		o_read_capacity_10 = 0x25,
		o_read_10          = 0x28,
		o_write_10         = 0x2a,
		o_write_verify_10  = 0x2e,
		o_read_16          = 0x88,
		o_write_16         = 0x8a,
		o_get_lba_status   = 0x9e,
		o_report_luns      = 0xa0,
	};

	std::optional<scsi_response> send(const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data);
};
