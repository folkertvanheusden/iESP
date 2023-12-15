#include <cassert>
#include <cstring>
#include <arpa/inet.h>

#include "iscsi-pdu.h"


std::string pdu_opcode_to_string(const iscsi_pdu_bhs::iscsi_bhs_opcode opcode)
{
	switch(opcode) {
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_nop_out:       return "I: NOP-Out";
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_scsi_cmd:      return "I: SCSI Command (encapsulates a SCSI Command Descriptor Block)";
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_scsi_taskman:  return "I: SCSI Task Management function request";
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_login_req:     return "I: Login Request";
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_text_req:      return "I: Text Request";
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_scsi_data_out: return "I: SCSI Data-Out (for WRITE operations)";
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_logout_req:    return "I: Logout Request";
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_snack_req:     return "I: SNACK Request";
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_nop_in:        return "T: NOP-In";
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_scsi_resp:     return "T: SCSI Response - contains SCSI status and possibly sense information or other response information.";
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_scsi_taskmanr: return "T: SCSI Task Management function response";
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_login_resp:    return "T: Login Response";
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_text_resp:     return "T: Text Response";
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_scsi_data_in:  return "T: SCSI Data-In - for READ operations";
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_logout_resp:   return "T: Logout Response";
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_r2t:           return "T: Ready To Transfer (R2T) - sent by target when it is ready to receive data";
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_async_msg:     return "T: Asynchronous Message - sent by target to indicate certain special conditions";
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_reject:        return "T: Reject";
	}

	return "???";
}

// BHS

iscsi_pdu_bhs::iscsi_pdu_bhs()
{
	assert(sizeof(bhs) == 48);

	bhs = { };
}

iscsi_pdu_bhs::~iscsi_pdu_bhs()
{
}

ssize_t iscsi_pdu_bhs::set(const uint8_t *const in, const size_t n)
{
	assert(n == 48);

	memcpy(&bhs, in, sizeof bhs);

	return sizeof bhs;
}

std::pair<const uint8_t *, std::size_t> iscsi_pdu_bhs::get()
{
	void *out = new uint8_t[sizeof bhs];
	memcpy(out, &bhs, sizeof bhs);

	return { reinterpret_cast<const uint8_t *>(out), sizeof bhs };
}

// AHS

iscsi_pdu_ahs::iscsi_pdu_ahs()
{
	assert(sizeof(__ahs_header__) == 3);
}

iscsi_pdu_ahs::~iscsi_pdu_ahs()
{
	delete ahs;
}

ssize_t iscsi_pdu_ahs::set(const uint8_t *const in, const size_t n)
{
	assert(n >= 3);

	size_t expected_size = ntohs(reinterpret_cast<const __ahs_header__ *>(in)->length);
	assert(expected_size + 3 == n);

	delete ahs;
	ahs = reinterpret_cast<__ahs_header__ *>(new uint8_t[expected_size + 3]);
	memcpy(ahs, in, n);

	return expected_size + 3;
}

std::pair<const uint8_t *, std::size_t> iscsi_pdu_ahs::get()
{
	uint16_t expected_size = ntohs(reinterpret_cast<const __ahs_header__ *>(ahs)->length);
	uint32_t out_size      = sizeof(__ahs_header__) + expected_size;
	void *out = new uint8_t[out_size];
	memcpy(out, ahs, out_size);

	return { reinterpret_cast<const uint8_t *>(out), out_size };
}

iscsi_pdu_login_request::iscsi_pdu_login_request()
{
	assert(sizeof(login_req) == 48);

	login_req = { };
}

iscsi_pdu_login_request::~iscsi_pdu_login_request()
{
}

ssize_t iscsi_pdu_login_request::set(const uint8_t *const in, const size_t n)
{
	assert(n == 48);

	memcpy(&login_req, in, sizeof login_req);

	return sizeof login_req;
}

std::pair<const uint8_t *, std::size_t> iscsi_pdu_login_request::get()
{
	void *out = new uint8_t[sizeof login_req];
	memcpy(out, &login_req, sizeof login_req);

	return { reinterpret_cast<const uint8_t *>(out), sizeof login_req };
}

iscsi_pdu_login_reply::iscsi_pdu_login_reply()
{
}

iscsi_pdu_login_reply::~iscsi_pdu_login_reply()
{
}

void iscsi_pdu_login_reply::set(const iscsi_pdu_login_request & reply_to)
{
	login_reply = { };
	login_reply.opcode     = 0x23;
	login_reply.T          = reply_to.get_T();
	login_reply.C          = reply_to.get_C();
	login_reply.CSG        = reply_to.get_CSG();
	login_reply.NSG        = reply_to.get_NSG();
	login_reply.versionmax = reply_to.get_versionmin();
	login_reply.versionact = reply_to.get_versionmin();
	login_reply.ahslen     = 0;
	// login_reply.datalenH / M / L;
	memcpy(login_reply.ISID, reply_to.get_ISID(), 6);
	login_reply.TSIH       = reply_to.get_TSIH();
	login_reply.Itasktag   = reply_to.get_Itasktag();
	login_reply.ExpCmdSN   = 1;  // won't handle re-login!
	login_reply.MaxStatSN  = 1;
}

std::pair<const uint8_t *, std::size_t> iscsi_pdu_login_reply::get()
{
	void *out = new uint8_t[sizeof login_reply];
	memcpy(out, &login_reply, sizeof login_reply);

	return { reinterpret_cast<const uint8_t *>(out), sizeof login_reply };
}
