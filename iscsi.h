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

	enum iscsi_opcode {
		// initiator opcodes
		o_nop_out       = 0x00,  // NOP-Out
		o_scsi_cmd      = 0x01,  // SCSI Command (encapsulates a SCSI Command Descriptor Block)
		o_scsi_taskman  = 0x02,  // SCSI Task Management function request
		o_login_req     = 0x03,  // Login Request
		o_text_req      = 0x04,  // Text Request
		o_scsi_data_out = 0x05,  // SCSI Data-Out (for WRITE operations)
		o_logout_req    = 0x06,  // Logout Request
		o_snack_req     = 0x10,  // SNACK Request
		// target opcodes
		o_nop_in        = 0x20,  // NOP-In
		o_scsi_resp     = 0x21,  // SCSI Response - contains SCSI status and possibly sense information or other response information.
		o_scsi_taskmanr = 0x22,  // SCSI Task Management function response
		o_login_resp    = 0x23,  // Login Response
		o_text_resp     = 0x24,  // Text Response
		o_scsi_data_in  = 0x25,  // SCSI Data-In - for READ operations.
		o_logout_resp   = 0x26,  // Logout Response
		o_r2t           = 0x31,  // Ready To Transfer (R2T) - sent by target when it is ready to receive data.
		o_async_msg     = 0x32,  // Asynchronous Message - sent by target to indicate certain special conditions.
		o_reject        = 0x3f,  // Reject
	};

	ssize_t set(const uint8_t *const in, const size_t n);
	std::pair<const uint8_t *, std::size_t> get();

	iscsi_opcode get_opcode() const { return iscsi_opcode(bhs.opcode); }
	void         set_opcode(const iscsi_opcode opcode) { bhs.opcode = opcode; }
};
