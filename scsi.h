#include <cstddef>
#include <cstdint>
#include <utility>


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
	};

	std::pair<uint8_t *, size_t> send(const uint8_t *const CDB, const size_t size);
};
