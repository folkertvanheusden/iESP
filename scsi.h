#include <cstddef>
#include <cstdint>
#include <utility>

#include "iscsi-pdu.h"


struct scsi_response
{
	std::vector<uint8_t>         sense_data;
	std::pair<uint8_t *, size_t> data;
};

class scsi
{
public:
	scsi();
	virtual ~scsi();

	enum scsi_opcode {
		o_test_unit_ready = 0x00,
		o_request_sense   = 0x02,
		o_read            = 0x08,
		o_write           = 0x0a,
		o_seek            = 0x0b,
		o_inquiry         = 0x12,
		o_read_capacity   = 0x25,
	};

	std::optional<scsi_response> send(const uint8_t *const CDB, const size_t size);
};
