#include <cstdint>
#include <string>
#include <utility>
#include <sys/types.h>


class iscsi_pdu_bhs  // basic header segment
{
public:
	struct __bhs__ {
		uint8_t  opcode    :  6;
		bool     I         :  1;
		bool     filler    :  1;
		uint32_t ospecf    : 23;  // opcode specific fields
		bool     F         :  1;
		uint8_t  ahslen    :  8;  // total ahs length (units of four byte words including padding)
		uint32_t datalenH  :  8;  // data segment length (bytes, excluding padding) 23...16
		uint32_t datalenM  :  8;  // data segment length (bytes, excluding padding) 15...8
		uint32_t datalenL  :  8;  // data segment length (bytes, excluding padding) 7...0
		uint8_t  lunfields[8];    // lun or opcode specific fields
		uint32_t Itasktag  : 32;  // initiator task tag
		uint8_t  ofields[28];     // opcode specific fields
	};

	__bhs__ bhs __attribute__((packed));

public:
	iscsi_pdu_bhs();
	virtual ~iscsi_pdu_bhs();

	enum iscsi_bhs_opcode {
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

	iscsi_bhs_opcode get_opcode()      const { return iscsi_bhs_opcode(bhs.opcode); }
	void             set_opcode(const iscsi_bhs_opcode opcode) { bhs.opcode = opcode; }

	size_t           get_ahs_length()  const { return bhs.ahslen;         }
	size_t           get_data_length() const { return (bhs.datalenH << 16) | (bhs.datalenM << 8) | bhs.datalenL; }
};

std::string pdu_opcode_to_string(const iscsi_pdu_bhs::iscsi_bhs_opcode opcode);

class iscsi_pdu_ahs
{
private:
	struct __ahs_header__ {
		uint16_t length: 16;
		uint8_t  type  :  8;
	} __attribute__((packed));

	__ahs_header__ *ahs { nullptr };

public:
	iscsi_pdu_ahs();
	virtual ~iscsi_pdu_ahs();

	enum iscsi_ahs_type {
		t_extended_cdb = 1,
		t_bi_data_len  = 2,  // Expected Bidirectional Read Data Length
	};

	ssize_t set(const uint8_t *const in, const size_t n);
	std::pair<const uint8_t *, std::size_t> get();

	iscsi_ahs_type get_ahs_type() { return iscsi_ahs_type(ahs->type >> 2); }
	void           set_ahs_type(const iscsi_ahs_type type) { ahs->type = type << 2; }
};

class iscsi_pdu_login_request : public iscsi_pdu_bhs  // login request
{
public:
	struct __login__ {
		uint8_t  opcode    :  6;
		bool     I_is_1    :  1;
		bool     filler    :  1;
		bool     T         :  1;
		bool     C         :  1;
		uint8_t  filler2   :  2;
		uint8_t  CSG       :  2;
		uint8_t  NSG       :  2;
		uint8_t  versionmax:  8;
		uint8_t  versionmin:  8;
		uint8_t  ahslen    :  8;  // total ahs length (units of four byte words including padding)
		uint32_t datalenH  :  8;  // data segment length (bytes, excluding padding) 23...16
		uint32_t datalenM  :  8;  // data segment length (bytes, excluding padding) 15...8
		uint32_t datalenL  :  8;  // data segment length (bytes, excluding padding) 7...0
		uint8_t  ISID[6];
		uint8_t  TSIH[2];
		uint32_t Itasktag  : 32;  // initiator task tag
		uint16_t CID       : 16;
		uint16_t reserved  : 16;
		uint32_t CmdSN     : 32;
		uint32_t ExpStatSN : 32;
		uint8_t  filler3[16];
	};

	__login__ login __attribute__((packed));

public:
	iscsi_pdu_login_request();
	virtual ~iscsi_pdu_login_request();

	ssize_t set(const uint8_t *const in, const size_t n);
	std::pair<const uint8_t *, std::size_t> get();

	const uint8_t *get_ISID() const { return login.ISID; }
};
