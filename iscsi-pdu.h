#pragma once
#include <cinttypes>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#if defined(RP2040W)
#include <lwip/tcp.h>
#include <lwip/sys.h>
#elif defined(TEENSY4_1)
//
#elif !defined(__MINGW32__)
#include <arpa/inet.h>
#endif
#include <sys/types.h>

#include "gen.h"
#include "iscsi.h"
#include "session.h"
#include "utils.h"


// These classes have a 'set' method as to have a way to return validity - an is_valid method would've worked as well.
// Also no direct retrieval from filedescriptors to help porting to platforms without socket-api.

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

	bool set(const uint8_t *const in, const size_t n);
	blob_t get();

	iscsi_ahs_type get_ahs_type() { return iscsi_ahs_type(ahs->type >> 2); }
	void           set_ahs_type(const iscsi_ahs_type type) { ahs->type = type << 2; }
};

struct iscsi_response_set;

class scsi;

class iscsi_pdu_bhs  // basic header segment
{
protected:
	uint8_t        pdu_bytes[48] { 0       };
	session *const ses           { nullptr };

private:
	struct __bhs__ {
		uint8_t  b1;
		// uint8_t  opcode    :  6;
		// bool     I         :  1;
		// bool     filler    :  1;

		uint32_t ospecf    : 24;  // opcode specific fields

		uint8_t  ahslen    :  8;  // total ahs length (units of four byte words including padding)
		uint32_t datalenH  :  8;  // data segment length (bytes, excluding padding) 23...16
		uint32_t datalenM  :  8;  // data segment length (bytes, excluding padding) 15...8
		uint32_t datalenL  :  8;  // data segment length (bytes, excluding padding) 7...0
		uint8_t  lunfields[8];    // lun or opcode specific fields
		uint32_t Itasktag  : 32;  // initiator task tag
		uint8_t  ofields[28];     // opcode specific fields
	} __attribute__((packed));

	__bhs__ *bhs __attribute__((packed)) { reinterpret_cast<__bhs__ *>(pdu_bytes) };

protected:
	std::vector<iscsi_pdu_ahs *> ahs_list;
	std::pair<uint8_t *, size_t> data     { nullptr, 0 };

	std::vector<blob_t> get_helper(const void *const header, const uint8_t *const data, const size_t data_len) const;

public:
	iscsi_pdu_bhs(session *const ses);
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

	virtual bool set(const uint8_t *const in, const size_t n);
	virtual std::vector<blob_t> get() const;

	size_t           get_ahs_length()  const { return bhs->ahslen * 4;                                              }
	bool             set_ahs_segment(std::pair<const uint8_t *, std::size_t> ahs_in);

	uint64_t         get_LUN_nr()      const { return *reinterpret_cast<const uint64_t *>(bhs->lunfields);          }

	iscsi_bhs_opcode get_opcode()      const { return iscsi_bhs_opcode(get_bits(bhs->b1, 0, 6));                    }
	blob_t           get_raw()         const;
	size_t           get_data_length() const { return (bhs->datalenH << 16) | (bhs->datalenM << 8) | bhs->datalenL; }
	std::optional<std::pair<const uint8_t *, size_t> > get_data() const;
	virtual bool     set_data(std::pair<const uint8_t *, std::size_t> data_in);

	virtual std::optional<iscsi_response_set> get_response(scsi *const sd);
};

struct iscsi_response_set
{
	std::vector<iscsi_pdu_bhs *> responses;
	bool r2t { false };

	std::optional<data_descriptor> to_stream;
};

std::string pdu_opcode_to_string(const iscsi_pdu_bhs::iscsi_bhs_opcode opcode);

std::optional<blob_t> generate_reject_pdu(const iscsi_pdu_bhs & about);

class iscsi_pdu_login_request : public iscsi_pdu_bhs  // login request 0x03
{
public:
	struct __login_req__ {
		uint8_t  b1;
		// uint8_t  opcode    :  6;
		// bool     I_is_1    :  1;
		// bool     filler    :  1;  // bit 7

		uint8_t  b2;
		// uint8_t  NSG       :  2;
		// uint8_t  CSG       :  2;
		// uint8_t  filler2   :  2;
		// bool     C         :  1;
		// bool     T         :  1;  // bit 7

		uint8_t  versionmax:  8;
		uint8_t  versionmin:  8;
		uint8_t  ahslen    :  8;  // total ahs length (units of four byte words including padding)
		uint32_t datalenH  :  8;  // data segment length (bytes, excluding padding) 23...16
		uint32_t datalenM  :  8;  // data segment length (bytes, excluding padding) 15...8
		uint32_t datalenL  :  8;  // data segment length (bytes, excluding padding) 7...0
		uint8_t  ISID[6];
		uint16_t TSIH;
		uint32_t Itasktag  : 32;  // initiator task tag
		uint16_t CID       : 16;
		uint16_t reserved  : 16;
		uint32_t CmdSN     : 32;
		uint32_t ExpStatSN : 32;
		uint8_t  filler3[16];
	} __attribute__((packed));

	__login_req__ *login_req __attribute__((packed)) { reinterpret_cast<__login_req__ *>(pdu_bytes) };

	std::optional<std::string> initiator;

public:
	iscsi_pdu_login_request(session *const ses);
	virtual ~iscsi_pdu_login_request();

	std::vector<blob_t> get() const override;

	const uint8_t *get_ISID()       const { return login_req->ISID;         }
	      uint16_t get_CID()        const { return login_req->CID;          }
	      uint32_t get_CmdSN()      const { return NTOHL(login_req->CmdSN); }
	      uint16_t get_TSIH()       const { return login_req->TSIH;         }
	      bool     get_T()          const { return get_bits(login_req->b2, 7, 1); }
	      bool     get_C()          const { return get_bits(login_req->b2, 6, 1); }
	      uint8_t  get_CSG()        const { return get_bits(login_req->b2, 2, 2); }
	      uint8_t  get_NSG()        const { return get_bits(login_req->b2, 0, 2); }
	      uint8_t  get_versionmin() const { return login_req->versionmin;   }
	      uint32_t get_Itasktag()   const { return login_req->Itasktag;     }
	      uint32_t get_ExpStatSN()  const { return NTOHL(login_req->ExpStatSN); }
	std::optional<std::string> get_initiator() const { return initiator;    }

	virtual bool   set_data(std::pair<const uint8_t *, std::size_t> data_in) override;
	virtual std::optional<iscsi_response_set> get_response(scsi *const sd) override;
};

class iscsi_pdu_login_reply : public iscsi_pdu_bhs
{
public:
	struct __login_reply__ {
		uint8_t  b1;
		// uint8_t  opcode    :  6;  // 0x21
		// bool     filler1   :  1;
		// bool     filler0   :  1;  // bit 7

		uint8_t  b2;
		// uint8_t  NSG       :  2;
		// uint8_t  CSG       :  2;
		// uint8_t  filler2   :  2;
		// bool     C         :  1;
		// bool     T         :  1;  // bit 7

		uint8_t  versionmax:  8;
		uint8_t  versionact:  8;
		uint8_t  ahslen    :  8;  // total ahs length (units of four byte words including padding)
		uint32_t datalenH  :  8;  // data segment length (bytes, excluding padding) 23...16
		uint32_t datalenM  :  8;  // data segment length (bytes, excluding padding) 15...8
		uint32_t datalenL  :  8;  // data segment length (bytes, excluding padding) 7...0
		uint8_t  ISID[6];
		uint16_t TSIH;
		uint32_t Itasktag  : 32;  // initiator task tag
		uint32_t reserved  : 32;
		uint32_t StatSN    : 32;
		uint32_t ExpCmdSN  : 32;
		uint32_t MaxCmdSN  : 32;
		uint8_t  statuscls :  8;  // status-class
		uint8_t  statusdet :  8;  // status-detail
		uint16_t reserved2 : 16;
		uint8_t  filler3[8];
	} __attribute__((packed));

	__login_reply__ *login_reply __attribute__((packed)) =  { reinterpret_cast<__login_reply__ *>(pdu_bytes) };

	std::pair<uint8_t *, size_t> login_reply_reply_data { nullptr, 0 };

public:
	iscsi_pdu_login_reply(session *const ses);
	virtual ~iscsi_pdu_login_reply();

	bool set(const iscsi_pdu_login_request & reply_to);
	std::vector<blob_t> get() const override;
};

class iscsi_pdu_scsi_cmd : public iscsi_pdu_bhs  // 0x01
{
public:
	struct __cdb_pdu_req__ {
		uint8_t  b1;
		// uint8_t  opcode    :  6;
		// bool     I         :  1;
		// bool     filler1   :  1;  // bit 7

		uint8_t  b2;
		// uint8_t  ATTR      :  3;
		// uint8_t  filler2   :  2;
		// bool     W         :  1;
		// bool     R         :  1;
		// bool     F         :  1;  // bit 7

		uint16_t reserved  ;
		uint8_t  ahslen    ;  // total ahs length (units of four byte words including padding)
		uint8_t  datalenH  ;  // data segment length (bytes, excluding padding) 23...16
		uint8_t  datalenM  ;  // data segment length (bytes, excluding padding) 15...8
		uint8_t  datalenL  ;  // data segment length (bytes, excluding padding) 7...0
		uint8_t  LUN[8];
		uint32_t Itasktag  ;  // initiator task tag
		uint32_t expdatlen ;  // expected data transfer lenth
		uint32_t CmdSN     ;
		uint32_t ExpStatSN ;
		uint8_t  CDB[16];
	} __attribute__((packed));

	__cdb_pdu_req__ *cdb_pdu_req __attribute__((packed)) { reinterpret_cast<__cdb_pdu_req__ *>(pdu_bytes) };

public:
	iscsi_pdu_scsi_cmd(session *const ses);
	virtual ~iscsi_pdu_scsi_cmd();

	bool set(const uint8_t *const in, const size_t n) override;
	std::vector<blob_t> get() const override;

	const uint8_t * get_CDB()       const { return cdb_pdu_req->CDB;              }
	      uint32_t  get_Itasktag()  const { return cdb_pdu_req->Itasktag;         }
	      uint32_t  get_ExpStatSN() const { return NTOHL(cdb_pdu_req->ExpStatSN); }
	      uint32_t  get_CmdSN()     const { return NTOHL(cdb_pdu_req->CmdSN);     }
	const uint8_t * get_LUN()       const { return cdb_pdu_req->LUN;              }
	      uint32_t  get_ExpDatLen() const { return NTOHL(cdb_pdu_req->expdatlen); }

	virtual std::optional<iscsi_response_set> get_response(scsi *const sd) override;
	// special case: response after one or more data-out PDUs
	std::optional<iscsi_response_set> get_response(scsi *const sd, const uint8_t error);
};

class iscsi_pdu_scsi_data_in : public iscsi_pdu_bhs  // 0x25
{
public:
	struct __pdu_data_in__ {
		uint8_t  b1;
		// uint8_t  opcode    :  6;
		// bool     reserved1 :  1;
		// bool     reserved0 :  1;  // bit 7

		uint8_t  b2;
		// bool     S         :  1;
		// bool     U         :  1;
		// bool     O         :  1;
		// bool     reserved5 :  1;
		// bool     reserved4 :  1;
		// bool     reserved3 :  1;
		// bool     A         :  1;
		// bool     F         :  1;  // bit 7

		uint8_t  reserved6 :  8;
		uint8_t  status    :  8;

		uint8_t  ahslen    :  8;  // total ahs length (units of four byte words including padding)
		uint32_t datalenH  :  8;  // data segment length (bytes, excluding padding) 23...16
		uint32_t datalenM  :  8;  // data segment length (bytes, excluding padding) 15...8
		uint32_t datalenL  :  8;  // data segment length (bytes, excluding padding) 7...0
		uint8_t  LUN[8];
		uint32_t Itasktag  : 32;  // initiator task tag
		uint32_t TTT       : 32;  // target transfer tag
		uint32_t StatSN    : 32;
		uint32_t ExpCmdSN  : 32;
		uint32_t MaxCmdSN  : 32;
		uint32_t DataSN    : 32;
		uint32_t bufferoff : 32;
		uint32_t ResidualCt: 32;  // residual count or reserved
	} __attribute__((packed));

	__pdu_data_in__ *pdu_data_in __attribute__((packed)) { reinterpret_cast<__pdu_data_in__ *>(pdu_bytes) };
	std::pair<uint8_t *, size_t> pdu_data_in_data { nullptr, 0 };

private:
	iscsi_pdu_scsi_cmd *reply_to_copy { nullptr };

public:
	iscsi_pdu_scsi_data_in(session *const ses);
	virtual ~iscsi_pdu_scsi_data_in();

	bool set(const iscsi_pdu_scsi_cmd & reply_to, const std::pair<uint8_t *, size_t> scsi_reply_data, const bool has_sense);
	std::vector<blob_t> get() const override;
        uint32_t get_TTT() const { return pdu_data_in->TTT; }

	static std::pair<blob_t, uint8_t *> gen_data_in_pdu(session *const ses, const iscsi_pdu_scsi_cmd & reply_to, const size_t use_pdu_data_size, const size_t offset_in_data, const size_t data_is_n_bytes);
};

class iscsi_pdu_scsi_data_out : public iscsi_pdu_bhs  // 0x05
{
public:
	struct __pdu_data_out__ {
		uint8_t  b1;
		// uint8_t  opcode    :  6;
		// bool     reserved0 :  2;  // bit 7

		uint8_t  b2;
		// bool     reserved1 :  7;
		// bool     F         :  1;  // bit 7

		uint16_t  reserved2: 16;

		uint8_t  ahslen    :  8;  // total ahs length (units of four byte words including padding)
		uint32_t datalenH  :  8;  // data segment length (bytes, excluding padding) 23...16
		uint32_t datalenM  :  8;  // data segment length (bytes, excluding padding) 15...8
		uint32_t datalenL  :  8;  // data segment length (bytes, excluding padding) 7...0
		uint8_t  LUN[8];
		uint32_t Itasktag  : 32;  // initiator task tag
		uint32_t TTT       : 32;  // target transfer tag
		uint32_t reserved3 : 32;
		uint32_t ExpStatSN : 32;
		uint32_t reserved4 : 32;
		uint32_t DataSN    : 32;
		uint32_t bufferoff : 32;
		uint32_t reserved5 : 32;  // residual count or reserved
	} __attribute__((packed));

	__pdu_data_out__ *pdu_data_out __attribute__((packed)) { reinterpret_cast<__pdu_data_out__ *>(pdu_bytes) };
	std::pair<uint8_t *, size_t> pdu_data_out_data { nullptr, 0 };

public:
	iscsi_pdu_scsi_data_out(session *const ses);
	virtual ~iscsi_pdu_scsi_data_out();

	bool set(const iscsi_pdu_scsi_cmd & reply_to, const std::pair<uint8_t *, size_t> scsi_reply_data);
	std::vector<blob_t> get() const override;

	uint32_t get_BufferOffset() const { return NTOHL(pdu_data_out->bufferoff); }
        uint32_t get_TTT()          const { return pdu_data_out->TTT;              }
	bool     get_F()            const { return !!(pdu_data_out->b2 & 128);     }
	uint32_t get_Itasktag()     const { return pdu_data_out->Itasktag;         }
	uint32_t get_ExpStatSN()    const { return pdu_data_out->ExpStatSN;        }
};

class iscsi_pdu_scsi_response : public iscsi_pdu_bhs  // 0x21
{
public:
	struct __pdu_response__ {
		uint8_t  b1;
		// uint8_t  opcode    :  6;
		// bool     reserved1 :  1;
		// bool     reserved0 :  1;  // bit 7

		uint8_t  b2;
		// bool     reserved3 :  1;
		// bool     U         :  1;
		// bool     O         :  1;
		// bool     u         :  1;
		// bool     o         :  1;
		// uint8_t  reserved2 :  2;
		// bool     set_to_1  :  1;  // 1, bit 7

		uint8_t  response  :  8;
		uint8_t  status    :  8;
		uint8_t  ahslen    :  8;  // total ahs length (units of four byte words including padding)
		uint32_t datalenH  :  8;  // data segment length (bytes, excluding padding) 23...16
		uint32_t datalenM  :  8;  // data segment length (bytes, excluding padding) 15...8
		uint32_t datalenL  :  8;  // data segment length (bytes, excluding padding) 7...0
		uint8_t  reserved5[8];
		uint32_t Itasktag  : 32;  // initiator task tag
		uint32_t snack_tag : 32;
		uint32_t StatSN    : 32;
		uint32_t ExpCmdSN  : 32;
		uint32_t MaxCmdSN  : 32;
		uint32_t ExpDataSN : 32;
		uint32_t BidirResCt: 32;  // bidirectional read residual count or reserved
		uint32_t ResidualCt: 32;  // residual count or reserved
	} __attribute__((packed));

	__pdu_response__ *pdu_response __attribute__((packed)) =  { reinterpret_cast<__pdu_response__ *>(pdu_bytes) };

	std::pair<uint8_t *, size_t> pdu_response_data { nullptr, 0 };

public:
	iscsi_pdu_scsi_response(session *const ses);
	virtual ~iscsi_pdu_scsi_response();

	bool set(const iscsi_pdu_scsi_cmd & reply_to, const std::vector<uint8_t> & scsi_sense_data, std::optional<uint32_t> ResidualCt);

	std::vector<blob_t> get() const override;
};

class iscsi_pdu_nop_out : public iscsi_pdu_bhs  // NOP-Out  0x00
{
public:
	struct __nop_out__ {
		uint8_t  b1;
		// uint8_t  opcode    :  6;
		// bool     I         :  1;
		// bool     filler1   :  1;  // bit 7

		uint8_t  b2;  // bit7 is 1
		uint16_t filler3   :  16;

		uint8_t  ahslen    :  8;  // total ahs length (units of four byte words including padding)
		uint32_t datalenH  :  8;  // data segment length (bytes, excluding padding) 23...16
		uint32_t datalenM  :  8;  // data segment length (bytes, excluding padding) 15...8
		uint32_t datalenL  :  8;  // data segment length (bytes, excluding padding) 7...0
		uint8_t  LUN[8];  // lun
		uint32_t Itasktag  : 32;  // initiator task tag
		uint32_t TTT       : 32;  // target transfer tag
		uint32_t CmdSN     : 32;
		uint32_t ExpStatSN : 32;
		uint8_t  filler4[16];
	} __attribute__((packed));

	__nop_out__ *nop_out __attribute__((packed)) { reinterpret_cast<__nop_out__ *>(pdu_bytes) };

public:
	iscsi_pdu_nop_out(session *const ses);
	virtual ~iscsi_pdu_nop_out();

	const uint8_t  *get_LUN()        const { return nop_out->LUN;              }
	      uint32_t  get_Itasktag()   const { return nop_out->Itasktag;         }
	      uint32_t  get_TTT()        const { return nop_out->TTT;              }
	      uint32_t  get_CmdSN()      const { return NTOHL(nop_out->CmdSN);     }
	      uint32_t  get_ExpStatSN()  const { return NTOHL(nop_out->ExpStatSN); }

	virtual std::optional<iscsi_response_set> get_response(scsi *const sd) override;
};

class iscsi_pdu_nop_in : public iscsi_pdu_bhs  // NOP-In
{
public:
	struct __nop_in__ {
		uint8_t  b1;
		// uint8_t  opcode    :  6;
		// bool     reserved1 :  1;
		// bool     reserved0 :  1;  // bit 7
		uint8_t  b2;  // bit 7 is 1
		uint16_t filler3;
		uint8_t  ahslen    :  8;  // total ahs length (units of four byte words including padding)
		uint32_t datalenH  :  8;  // data segment length (bytes, excluding padding) 23...16
		uint32_t datalenM  :  8;  // data segment length (bytes, excluding padding) 15...8
		uint32_t datalenL  :  8;  // data segment length (bytes, excluding padding) 7...0
		uint8_t  LUN[8];  // lun
		uint32_t Itasktag  : 32;  // initiator task tag
		uint32_t TTT       : 32;  // target transfer tag
		uint32_t StatSN    : 32;
		uint32_t ExpCmdSN  : 32;
		uint32_t MaxCmdSN  : 32;
		uint8_t  filler4[12];
	} __attribute__((packed));

	__nop_in__ *nop_in __attribute__((packed)) { reinterpret_cast<__nop_in__ *>(pdu_bytes) };

public:
	iscsi_pdu_nop_in(session *const ses);
	virtual ~iscsi_pdu_nop_in();

	bool set(const iscsi_pdu_nop_out & reply_to);
	std::vector<blob_t> get() const override;
};

class iscsi_pdu_scsi_r2t : public iscsi_pdu_bhs  // 0x31
{
public:
	struct __pdu_scsi_r2t__ {
		uint8_t  b1;
		// uint8_t  opcode    :  6;
		// bool     reserved1 :  1;
		// bool     reserved0 :  1;  // bit 7

		uint8_t  b2;  // bit 7 is 1
		uint16_t filler3;

		uint8_t  ahslen    :  8;  // total ahs length (units of four byte words including padding)
		uint32_t datalenH  :  8;  // data segment length (bytes, excluding padding) 23...16
		uint32_t datalenM  :  8;  // data segment length (bytes, excluding padding) 15...8
		uint32_t datalenL  :  8;  // data segment length (bytes, excluding padding) 7...0
		uint8_t  LUN[8];
		uint32_t Itasktag  : 32;  // initiator task tag
		uint32_t TTT       : 32;  // target transfer tag
		uint32_t StatSN    : 32;
		uint32_t ExpCmdSN  : 32;
		uint32_t MaxCmdSN  : 32;
		uint32_t R2TSN     : 32;
		uint32_t bufferoff : 32;
		uint32_t DDTF      : 32;  // desired data transfer length
	} __attribute__((packed));

	__pdu_scsi_r2t__ *pdu_scsi_r2t __attribute__((packed)) { reinterpret_cast<__pdu_scsi_r2t__ *>(pdu_bytes) };

	std::pair<uint8_t *, size_t> pdu_scsi_r2t_data { nullptr, 0 };

public:
	iscsi_pdu_scsi_r2t(session *const ses);
	virtual ~iscsi_pdu_scsi_r2t();

	bool set(const iscsi_pdu_scsi_cmd & reply_to, const uint32_t TTT, const uint32_t buffer_offset, const uint32_t data_length);
	std::vector<blob_t> get() const override;

	uint32_t get_TTT() const { return pdu_scsi_r2t->TTT; }

	std::optional<iscsi_response_set> get_response(scsi *const sd) override { return { }; }
};

class iscsi_pdu_text_request : public iscsi_pdu_bhs  // text request 0x04
{
public:
	struct __text_req__ {
		uint8_t  b1;
		// uint8_t  opcode    :  6;
		// bool     I_is_1    :  1;
		// bool     filler    :  1;  // bit 7

		uint8_t  b2;
		// 6 bit filler
		// bool     C         :  1;
		// bool     F         :  1;  // bit 7
		uint16_t filler2   :  16;

		uint8_t  ahslen    :  8;  // total ahs length (units of four byte words including padding)
		uint32_t datalenH  :  8;  // data segment length (bytes, excluding padding) 23...16
		uint32_t datalenM  :  8;  // data segment length (bytes, excluding padding) 15...8
		uint32_t datalenL  :  8;  // data segment length (bytes, excluding padding) 7...0
		uint8_t  LUN[8];
		uint32_t Itasktag  : 32;  // initiator task tag
		uint32_t TTT       : 32;  // target transfer tag
		uint32_t CmdSN     : 32;
		uint32_t ExpStatSN : 32;
		uint8_t  filler3[16];
	} __attribute__((packed));

	__text_req__ *text_req __attribute__((packed)) { reinterpret_cast<__text_req__ *>(pdu_bytes) };

public:
	iscsi_pdu_text_request(session *const ses);
	virtual ~iscsi_pdu_text_request();

	bool set(const uint8_t *const in, const size_t n) override;
	std::vector<blob_t> get() const override;

	const uint8_t * get_LUN()      const { return text_req->LUN;              }
	      uint32_t get_CmdSN()     const { return NTOHL(text_req->CmdSN);     }
	      uint32_t get_Itasktag()  const { return text_req->Itasktag;         }
	      uint32_t get_ExpStatSN() const { return NTOHL(text_req->ExpStatSN); }
              uint32_t get_TTT()       const { return text_req->TTT;              }

	virtual std::optional<iscsi_response_set> get_response(scsi *const sd) override;
};

class iscsi_pdu_text_reply : public iscsi_pdu_bhs  // 0x24
{
public:
	struct __text_reply__ {
		uint8_t  b1;
		// uint8_t  opcode    :  6;
		// bool     filler    :  2;  // bit 7

		uint8_t  b2;
		// 6 bit filler
		// bool     C         :  1;
		// bool     F         :  1;  // bit 7
		uint16_t filler2   :  16;

		uint8_t  ahslen    :  8;  // total ahs length (units of four byte words including padding)
		uint32_t datalenH  :  8;  // data segment length (bytes, excluding padding) 23...16
		uint32_t datalenM  :  8;  // data segment length (bytes, excluding padding) 15...8
		uint32_t datalenL  :  8;  // data segment length (bytes, excluding padding) 7...0
		uint8_t  LUN[8];
		uint32_t Itasktag  : 32;  // initiator task tag
		uint32_t TTT       : 32;  // target transfer tag
		uint32_t StatSN    : 32;
		uint32_t ExpCmdSN  : 32;
		uint32_t MaxCmdSN  : 32;
		uint8_t  filler3[12];
	} __attribute__((packed));

	__text_reply__ *text_reply __attribute__((packed)) =  { reinterpret_cast<__text_reply__ *>(pdu_bytes) };

	std::pair<uint8_t *, size_t> text_reply_reply_data { nullptr, 0 };

public:
	iscsi_pdu_text_reply(session *const ses);
	virtual ~iscsi_pdu_text_reply();

	bool set(const iscsi_pdu_text_request & reply_to, scsi *const sd);
	std::vector<blob_t> get() const override;
};

class iscsi_pdu_logout_request : public iscsi_pdu_bhs  // logout request 0x06
{
public:
	struct __logout_req__ {
		uint8_t  b1;
		// uint8_t  opcode    :  6;  // 0x06
		// bool     filler1   :  1;
		// bool     filler0   :  1;  // bit 7

		uint8_t  b2;  // bit 7 is 1, 6...0 is reason code
		uint16_t filler1_2;

		uint8_t  ahslen    :  8;  // total ahs length (units of four byte words including padding)
		uint32_t datalenH  :  8;  // data segment length (bytes, excluding padding) 23...16
		uint32_t datalenM  :  8;  // data segment length (bytes, excluding padding) 15...8
		uint32_t datalenL  :  8;  // data segment length (bytes, excluding padding) 7...0
		uint8_t  filler3[8];
		uint32_t Itasktag  : 32;  // initiator task tag
		uint32_t reserved  : 32;
		uint32_t CmdSN     : 32;
		uint32_t ExpStatSN : 32;
		uint8_t  filler4[16];
	} __attribute__((packed));

	__logout_req__ *logout_req __attribute__((packed)) { reinterpret_cast<__logout_req__ *>(pdu_bytes) };

public:
	iscsi_pdu_logout_request(session *const ses);
	virtual ~iscsi_pdu_logout_request();

	bool set(const uint8_t *const in, const size_t n) override;
	std::vector<blob_t> get() const override;

	uint32_t get_CmdSN()      const { return NTOHL(logout_req->CmdSN); }
	uint32_t get_Itasktag()   const { return logout_req->Itasktag;     }
	uint32_t get_ExpStatSN()  const { return NTOHL(logout_req->ExpStatSN); }

	virtual std::optional<iscsi_response_set> get_response(scsi *const sd) override;
};

class iscsi_pdu_logout_reply : public iscsi_pdu_bhs
{
public:
	struct __logout_reply__ {
		uint8_t  b1;
		// uint8_t  opcode    :  6;  // 0x26
		// bool     filler1   :  1;
		// bool     filler0   :  1;  // bit 7

		uint8_t  b2;  // bit 7 is 1
		uint8_t  response;
		uint8_t  filler2;
		uint8_t  ahslen    :  8;  // total ahs length (units of four byte words including padding)
		uint32_t datalenH  :  8;  // data segment length (bytes, excluding padding) 23...16
		uint32_t datalenM  :  8;  // data segment length (bytes, excluding padding) 15...8
		uint32_t datalenL  :  8;  // data segment length (bytes, excluding padding) 7...0
		uint8_t  filler3[8];
		uint32_t Itasktag  : 32;  // initiator task tag
		uint32_t filler4   : 32;
		uint32_t StatSN    : 32;
		uint32_t ExpCmdSN  : 32;
		uint32_t MaxCmdSN  : 32;
		uint32_t filler5   : 32;
		uint16_t Time2Wait : 16;
		uint16_t Time2Ret  : 16;
		uint32_t filler6   : 32;
	} __attribute__((packed));

	__logout_reply__ *logout_reply __attribute__((packed)) =  { reinterpret_cast<__logout_reply__ *>(pdu_bytes) };

	std::pair<uint8_t *, size_t> logout_reply_reply_data { nullptr, 0 };

public:
	iscsi_pdu_logout_reply(session *const ses);
	virtual ~iscsi_pdu_logout_reply();

	bool set(const iscsi_pdu_logout_request & reply_to);
	std::vector<blob_t> get() const override;
};

///

class iscsi_pdu_taskman_request : public iscsi_pdu_bhs  // taskman request 0x02
{
public:
	struct __taskman_req__ {
		uint8_t  b1;
		// uint8_t  opcode    :  6;  // 0x02
		// bool     I         :  1;
		// bool     filler0   :  1;  // bit 7

		uint8_t  b2;  // bit 7 is 1, 6...0 is function
		uint16_t filler1_2;

		uint8_t  ahslen    :  8;  // total ahs length (units of four byte words including padding)
		uint32_t datalenH  :  8;  // data segment length (bytes, excluding padding) 23...16
		uint32_t datalenM  :  8;  // data segment length (bytes, excluding padding) 15...8
		uint32_t datalenL  :  8;  // data segment length (bytes, excluding padding) 7...0
		uint8_t  LUN[8];
		uint32_t Itasktag  : 32;  // initiator task tag
		uint32_t RefTaskTag: 32;
		uint32_t CmdSN     : 32;
		uint32_t ExpStatSN : 32;
		uint32_t RefCmdSN  : 32;
		uint32_t ExpDatSN  : 32;
		uint8_t  filler3[8];
	} __attribute__((packed));

	__taskman_req__ *taskman_req __attribute__((packed)) { reinterpret_cast<__taskman_req__ *>(pdu_bytes) };

public:
	iscsi_pdu_taskman_request(session *const ses);
	virtual ~iscsi_pdu_taskman_request();

	bool set(const uint8_t *const in, const size_t n) override;
	std::vector<blob_t> get() const override;

	uint32_t get_Itasktag()  const { return taskman_req->Itasktag;         }
	uint32_t get_ExpStatSN() const { return NTOHL(taskman_req->ExpStatSN); }
	uint32_t get_CmdSN()     const { return NTOHL(taskman_req->CmdSN);     }

	virtual std::optional<iscsi_response_set> get_response(scsi *const sd) override;
};

class iscsi_pdu_taskman_reply : public iscsi_pdu_bhs
{
public:
	struct __taskman_reply__ {
		uint8_t  b1;
		// uint8_t  opcode    :  6;  // 0x22
		// bool     filler0   :  2;  // bit 7

		uint8_t  b2;  // bit 7 is 1
		uint8_t  response;
		uint8_t  filler1;

		uint8_t  ahslen    :  8;  // total ahs length (units of four byte words including padding)
		uint32_t datalenH  :  8;  // data segment length (bytes, excluding padding) 23...16
		uint32_t datalenM  :  8;  // data segment length (bytes, excluding padding) 15...8
		uint32_t datalenL  :  8;  // data segment length (bytes, excluding padding) 7...0
		uint8_t  filler2[8];
		uint32_t Itasktag  : 32;  // initiator task tag
		uint32_t filler3   : 32;
		uint32_t StatSN    : 32;
		uint32_t ExpCmdSN  : 32;
		uint32_t MaxCmdSN  : 32;
		uint8_t  filler4[12];
	} __attribute__((packed));

	__taskman_reply__ *taskman_reply __attribute__((packed)) =  { reinterpret_cast<__taskman_reply__ *>(pdu_bytes) };

public:
	iscsi_pdu_taskman_reply(session *const ses);
	virtual ~iscsi_pdu_taskman_reply();

	bool set(const iscsi_pdu_taskman_request & reply_to);
	std::vector<blob_t> get() const override;
};
