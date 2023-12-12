#include <cstdint>
#include <utility>
#include <sys/types.h>


class iscsi_pdu_bhs  // basic header segment
{
private:
	struct __bhs__ {
		bool     filler    :  1;
		bool     I         :  1;
		uint8_t  opcode    :  6;
		bool     F         :  1;
		uint32_t ospecf    : 23;  // opcode specific fields
		uint8_t  ahslen    :  8;  // total ahs length
		uint32_t datalen   : 24;  // data segment length
		uint8_t  lunfields[8];    // lun or opcode specific fields
		uint32_t Itasktag  : 32;  // initiator task tag
		uint8_t  ofields[28];     // opcode specific fields
	} bhs __attribute__((packed));

public:
	iscsi_pdu_bhs();
	virtual ~iscsi_pdu_bhs();

	ssize_t set(const uint8_t *const in, const size_t n);
	std::pair<const uint8_t *, std::size_t> get();
};
