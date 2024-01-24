#include <cstddef>
#include <cstdint>
#include <utility>

#include "backend.h"
#include "iscsi.h"
#include "iscsi-pdu.h"


struct scsi_response
{
	iscsi_reacion_t              type;
	std::vector<uint8_t>         sense_data;
	std::pair<uint8_t *, size_t> data;
	
	uint32_t buffer_offset;
	uint32_t buffer_segment_length;
};

class scsi
{
private:
	backend *const b { nullptr };

public:
	scsi(backend *const b);
	virtual ~scsi();

	enum scsi_opcode {
		o_test_unit_ready  = 0x00,
		o_request_sense    = 0x02,
		o_read             = 0x08,
		o_write            = 0x0a,
		o_seek             = 0x0b,
		o_inquiry          = 0x12,
		o_read_capacity_10 = 0x25,
		o_write_10         = 0x2a,
		o_read_16          = 0x88,
		o_write_16         = 0x8a,
		o_get_lba_status   = 0x9e,
		o_report_luns      = 0xa0,
	};

	std::optional<scsi_response> send(const uint8_t *const CDB, const size_t size, std::optional<std::pair<uint8_t *, size_t> > & data);
};
