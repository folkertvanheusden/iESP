#include <cassert>
#include <cstring>
#include <vector>
#include <arpa/inet.h>

#include "iscsi-pdu.h"
#include "log.h"
#include "scsi.h"


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

/*--------------------------------------------------------------------------*/
// BHS

iscsi_pdu_bhs::iscsi_pdu_bhs()
{
	assert(sizeof(*bhs) == 48);

	*bhs = { };
}

iscsi_pdu_bhs::~iscsi_pdu_bhs()
{
	for(auto & ahs : ahs_list)
		delete ahs;
}

bool iscsi_pdu_bhs::set(session *const s, const uint8_t *const in, const size_t n)
{
	if (n != 48)
		return false;

	memcpy(pdu_bytes, in, sizeof *bhs);

	// TODO validate against session

	return true;
}

bool iscsi_pdu_bhs::set_ahs_segment(std::pair<const uint8_t *, std::size_t> ahs_in)
{
	size_t offset = 0;

	while(offset + 3 < ahs_in.second) {
		size_t ahs_length = (ahs_in.first[offset + 0] << 8) | ahs_in.first[offset + 1];
		if (offset + 3 + ahs_length > ahs_in.second)
			break;

		iscsi_pdu_ahs *instance = new iscsi_pdu_ahs();
		if (instance->set(&ahs_in.first[offset], ahs_length) == false) {
			delete instance;
			break;
		}

		ahs_list.push_back(instance);
	}

	return offset == ahs_in.second;
}

bool iscsi_pdu_bhs::set_data(std::pair<const uint8_t *, std::size_t> data_in)
{
	if (data_in.second == 0 || data_in.second > 16777215)
		return false;

	data.second = data_in.second;

	data.first  = new uint8_t[data_in.second];
	memcpy(data.first, data_in.first, data_in.second);

	return true;
}

std::optional<std::pair<uint8_t *, size_t> > iscsi_pdu_bhs::get_data()
{
	if (data.second == 0)
		return { };

	uint8_t *out = new uint8_t[data.second];
	memcpy(out, data.first, data.second);

	return { { out, data.second } };
}

std::pair<const uint8_t *, std::size_t> iscsi_pdu_bhs::get()
{
	void *out = new uint8_t[sizeof *bhs];
	memcpy(out, &bhs, sizeof *bhs);

	return { reinterpret_cast<const uint8_t *>(out), sizeof *bhs };
}

std::optional<iscsi_response_set> iscsi_pdu_bhs::get_response(const iscsi_response_parameters *const parameters_in, std::optional<std::pair<uint8_t *, size_t> > data)
{
	DOLOG("iscsi_pdu_bhs::get_response invoked!\n");
	assert(0);
	return { };
}

/*--------------------------------------------------------------------------*/
// AHS

iscsi_pdu_ahs::iscsi_pdu_ahs()
{
	assert(sizeof(__ahs_header__) == 3);
}

iscsi_pdu_ahs::~iscsi_pdu_ahs()
{
	delete ahs;
}

bool iscsi_pdu_ahs::set(const uint8_t *const in, const size_t n)
{
	if (n < 3)
		return false;

	size_t expected_size = ntohs(reinterpret_cast<const __ahs_header__ *>(in)->length);
	if (expected_size + 3 != n)
		return false;

	ahs = reinterpret_cast<__ahs_header__ *>(new uint8_t[expected_size + 3]);
	memcpy(ahs, in, n);

	return true;
}

std::pair<const uint8_t *, std::size_t> iscsi_pdu_ahs::get()
{
	uint16_t expected_size = ntohs(reinterpret_cast<const __ahs_header__ *>(ahs)->length);
	uint32_t out_size      = sizeof(__ahs_header__) + expected_size;
	void *out = new uint8_t[out_size];
	memcpy(out, ahs, out_size);

	return { reinterpret_cast<const uint8_t *>(out), out_size };
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_login_request::iscsi_pdu_login_request()
{
	assert(sizeof(*login_req) == 48);

	*login_req = { };
}

iscsi_pdu_login_request::~iscsi_pdu_login_request()
{
}

bool iscsi_pdu_login_request::set(session *const s, const uint8_t *const in, const size_t n)
{
	if (iscsi_pdu_bhs::set(s, in, n) == false)
		return false;

	// TODO further validation

	return true;
}

std::pair<const uint8_t *, std::size_t> iscsi_pdu_login_request::get()
{
	void *out = new uint8_t[sizeof *login_req];
	memcpy(out, login_req, sizeof *login_req);

	return { reinterpret_cast<const uint8_t *>(out), sizeof *login_req };
}

std::optional<iscsi_response_set> iscsi_pdu_login_request::get_response(const iscsi_response_parameters *const parameters_in, std::optional<std::pair<uint8_t *, size_t> > data)
{
	auto parameters = static_cast<const iscsi_response_parameters_login_req *>(parameters_in);

	iscsi_response_set response;
	auto reply_pdu = new iscsi_pdu_login_reply();
	if (reply_pdu->set(*this) == false) {
		delete reply_pdu;
		return { };
	}
	response.responses.push_back(reply_pdu);

	return response;
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_login_reply::iscsi_pdu_login_reply()
{
	assert(sizeof(*login_reply) == 48);
}

iscsi_pdu_login_reply::~iscsi_pdu_login_reply()
{
}

bool iscsi_pdu_login_reply::set(const iscsi_pdu_login_request & reply_to)
{
	const std::vector<std::string> kvs {
		"HeaderDigest=None",
		"DataDigest=None",
		"MaxConnections=1",
		"TargetName=test",  // TODO
		"TargetAlias=Bob-s disk",  // TODO
		"TargetPortalGroupTag=1",
		"ImmediateData=Yes",
		"MaxRecvDataSegmentLength=4096",
		"MaxBurstLength=4096",
		"FirstBurstLength=4096",
		"TargetPortalGroupTag=1",
		"InitialR2T=Yes",
		"MaxOutstandingR2T=1",
		"DefaultTime2Wait=1",
		"DefaultTime2Retain=0",
		"IFMarker=No",
		"OFMarker=No",
		"ErrorRecoveryLevel=0",
		"DataPDUInOrder=Yes",
		"DataSequenceInOrder=Yes",
	};
	// determine total length
	login_reply_reply_data.second = 0;
	for(const auto & kv: kvs)
		login_reply_reply_data.second += kv.size() + 1;

	login_reply_reply_data.first = new uint8_t[login_reply_reply_data.second + 4/*padding*/]();
	size_t data_offset = 0;
	for(const auto & kv: kvs) {
		memcpy(&login_reply_reply_data.first[data_offset], kv.c_str(), kv.size());
		data_offset += kv.size() + 1;  // for 0x00
	}
	assert(data_offset == login_reply_reply_data.second);

	*login_reply = { };
	login_reply->opcode     = o_login_resp;  // 0x23
	login_reply->filler0    = false;
	login_reply->filler1    = false;
	login_reply->T          = true;
	login_reply->C          = false;
	login_reply->CSG        = reply_to.get_CSG();
	login_reply->NSG        = reply_to.get_NSG();
	login_reply->versionmax = reply_to.get_versionmin();
	login_reply->versionact = reply_to.get_versionmin();
	login_reply->ahslen     = 0;
	login_reply->datalenH   = login_reply_reply_data.second >> 16;
	login_reply->datalenM   = login_reply_reply_data.second >>  8;
	login_reply->datalenL   = login_reply_reply_data.second      ;
	memcpy(login_reply->ISID, reply_to.get_ISID(), 6);
	login_reply->TSIH       = reply_to.get_TSIH();
	login_reply->Itasktag   = reply_to.get_Itasktag();
	login_reply->StatSN     = htonl(reply_to.get_ExpStatSN());
	login_reply->ExpCmdSN   = htonl(reply_to.get_CmdSN());
	login_reply->MaxStatSN  = htonl(reply_to.get_CmdSN());

	return true;
}

std::pair<const uint8_t *, std::size_t> iscsi_pdu_login_reply::get()
{
	// round for padding
	size_t data_size_padded = (login_reply_reply_data.second + 3) & ~3;

	size_t out_size = sizeof(*login_reply) + data_size_padded;
	uint8_t *out = new uint8_t[out_size];
	memcpy(out, login_reply, sizeof *login_reply);
	memcpy(&out[sizeof *login_reply], login_reply_reply_data.first, login_reply_reply_data.second);

	return { out, out_size };
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_scsi_cmd::iscsi_pdu_scsi_cmd()
{
	assert(sizeof(*cdb_pdu_req) == 48);
}

iscsi_pdu_scsi_cmd::~iscsi_pdu_scsi_cmd()
{
}

bool iscsi_pdu_scsi_cmd::set(session *const s, const uint8_t *const in, const size_t n)
{
	if (iscsi_pdu_bhs::set(s, in, n) == false) {
		DOLOG("iscsi_pdu_scsi_cmd::set: iscsi_pdu_bhs::set returned error state\n");
		return false;
	}

	// TODO further validation

	return true;
}

std::pair<const uint8_t *, std::size_t> iscsi_pdu_scsi_cmd::get()
{
	void *out = new uint8_t[sizeof cdb_pdu_req];
	memcpy(out, &cdb_pdu_req, sizeof cdb_pdu_req);

	return { reinterpret_cast<const uint8_t *>(out), sizeof cdb_pdu_req };
}

std::optional<iscsi_response_set> iscsi_pdu_scsi_cmd::get_response(const iscsi_response_parameters *const parameters_in, std::optional<std::pair<uint8_t *, size_t> > data)
{
	auto parameters = static_cast<const iscsi_response_parameters_scsi_cmd *>(parameters_in);
	auto scsi_reply = parameters->sd->send(get_CDB(), 16, data);
	if (scsi_reply.has_value() == false) {
		DOLOG("iscsi_pdu_scsi_cmd::get_response: scsi::send returned nothing\n");
		return { };
	}

	iscsi_response_set response;
	bool               ok       { true };

	if (scsi_reply.value().data.second) {
		auto pdu_data_in = new iscsi_pdu_scsi_data_in();  // 0x25
		DOLOG("iscsi_pdu_scsi_cmd::get_response: sending SCSI DATA-IN with %zu payload bytes\n", scsi_reply.value().data.second);
		if (pdu_data_in->set(*this, scsi_reply.value().data) == false) {
			ok = false;
			DOLOG("iscsi_pdu_scsi_cmd::get_response: iscsi_pdu_scsi_data_in::set returned error state\n");
		}
		response.responses.push_back(pdu_data_in);
	}
	else {
		assert(scsi_reply.value().data.first == nullptr);
	}

	iscsi_pdu_bhs *pdu_scsi_response = nullptr;
	if (scsi_reply.value().type == ir_as_is) {
		auto *temp = new iscsi_pdu_scsi_response() /* 0x21 */;
		DOLOG("iscsi_pdu_scsi_cmd::get_response: sending SCSI response with %zu sense bytes\n", scsi_reply.value().sense_data.size());

		if (temp->set(*this, scsi_reply.value().sense_data) == false) {
			ok = false;
			DOLOG("iscsi_pdu_scsi_cmd::get_response: iscsi_pdu_scsi_response::set returned error\n");
		}
		pdu_scsi_response = temp;
	}
	else if (scsi_reply.value().type == ir_r2t) {
		auto *temp = new iscsi_pdu_scsi_r2t() /* 0x31 */;
		DOLOG("iscsi_pdu_scsi_cmd::get_response: sending R2T with %zu sense bytes\n", scsi_reply.value().sense_data.size());

		if (temp->set(*this, scsi_reply.value().buffer_offset, scsi_reply.value().buffer_segment_length) == false) {
			ok = false;
			DOLOG("iscsi_pdu_scsi_cmd::get_response: iscsi_pdu_scsi_response::set returned error\n");
		}
		pdu_scsi_response = temp;
	}
	else {
		DOLOG("iscsi_pdu_scsi_cmd::get_response: internal error\n");
		ok = false;
	}
	response.responses.push_back(pdu_scsi_response);

	if (!ok) {
		for(auto & r: response.responses)
			delete r;

		DOLOG("iscsi_pdu_scsi_cmd::get_response: failed\n");

		return { };
	}

	return response;
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_scsi_response::iscsi_pdu_scsi_response()
{
	assert(sizeof(*pdu_response) == 48);
}

iscsi_pdu_scsi_response::~iscsi_pdu_scsi_response()
{
}

bool iscsi_pdu_scsi_response::set(const iscsi_pdu_scsi_cmd & reply_to, const std::vector<uint8_t> & scsi_sense_data)
{
	size_t sense_data_size = scsi_sense_data.size();
	size_t reply_data_plus_sense_header = sense_data_size > 0 ? 2 + sense_data_size : 0;

	*pdu_response = { };
	pdu_response->opcode     = o_scsi_resp;  // 0x21
//	pdu_response->U          = true;
	pdu_response->set_to_1   = true;
	pdu_response->datalenH   = reply_data_plus_sense_header >> 16;
	pdu_response->datalenM   = reply_data_plus_sense_header >>  8;
	pdu_response->datalenL   = reply_data_plus_sense_header      ;
	pdu_response->Itasktag   = reply_to.get_Itasktag();
	pdu_response->StatSN     = htonl(reply_to.get_ExpStatSN());
	pdu_response->ExpCmdSN   = htonl(reply_to.get_CmdSN() + 1);
	pdu_response->MaxCmdSN   = htonl(reply_to.get_CmdSN() + 1);
	pdu_response->ExpDataSN  = htonl(1);  // TODO
	pdu_response->ResidualCt = 0;

	pdu_response_data.second = reply_data_plus_sense_header;
	if (pdu_response_data.second) {
		pdu_response->status       = 2;  // check condition

		pdu_response_data.first    = new uint8_t[pdu_response_data.second]();
		pdu_response_data.first[0] = sense_data_size >> 8;
		pdu_response_data.first[1] = sense_data_size;
		memcpy(pdu_response_data.first + 2, scsi_sense_data.data(), sense_data_size);
	}

	return true;
}

std::pair<const uint8_t *, std::size_t> iscsi_pdu_scsi_response::get()
{
	size_t out_size = sizeof(*pdu_response) + pdu_response_data.second;
	uint8_t *out = new uint8_t[out_size];
	memcpy(out, pdu_response, sizeof *pdu_response);
	memcpy(&out[sizeof *pdu_response], pdu_response_data.first, pdu_response_data.second);

	return { out, out_size };
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_scsi_data_in::iscsi_pdu_scsi_data_in()
{
	assert(sizeof(*pdu_data_in) == 48);
}

iscsi_pdu_scsi_data_in::~iscsi_pdu_scsi_data_in()
{
}

bool iscsi_pdu_scsi_data_in::set(const iscsi_pdu_scsi_cmd & reply_to, const std::pair<uint8_t *, size_t> scsi_reply_data)
{
	DOLOG("iscsi_pdu_scsi_data_in::set: with %zu payload bytes\n", scsi_reply_data.second);

	*pdu_data_in = { };
	pdu_data_in->opcode     = o_scsi_data_in;  // 0x25
	pdu_data_in->datalenH   = scsi_reply_data.second >> 16;
	pdu_data_in->datalenM   = scsi_reply_data.second >>  8;
	pdu_data_in->datalenL   = scsi_reply_data.second      ;
	memcpy(pdu_data_in->LUN, reply_to.get_LUN(), sizeof pdu_data_in->LUN);
	pdu_data_in->Itasktag   = reply_to.get_Itasktag();
	pdu_data_in->StatSN     = htonl(reply_to.get_ExpStatSN());
	pdu_data_in->ExpCmdSN   = htonl(reply_to.get_CmdSN() + 1);
	pdu_data_in->MaxCmdSN   = htonl(reply_to.get_CmdSN() + 1);
	pdu_data_in->DataSN     = htonl(1);
	pdu_data_in->ResidualCt = 0;

	pdu_data_in_data.second = scsi_reply_data.second;
	pdu_data_in_data.first  = new uint8_t[pdu_data_in_data.second]();
	memcpy(pdu_data_in_data.first, scsi_reply_data.first, pdu_data_in_data.second);

	return true;
}

std::pair<const uint8_t *, std::size_t> iscsi_pdu_scsi_data_in::get()
{
	size_t out_size = sizeof(*pdu_data_in) + pdu_data_in_data.second;
	uint8_t *out = new uint8_t[out_size];
	memcpy(out, pdu_data_in, sizeof *pdu_data_in);
	memcpy(&out[sizeof(*pdu_data_in)], pdu_data_in_data.first, pdu_data_in_data.second);

	return { out, out_size };
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_nop_out::iscsi_pdu_nop_out()
{
	assert(sizeof(*nop_out) == 48);
}

iscsi_pdu_nop_out::~iscsi_pdu_nop_out()
{
}

std::optional<iscsi_response_set> iscsi_pdu_nop_out::get_response(const iscsi_response_parameters *const parameters_in, std::optional<std::pair<uint8_t *, size_t> > data)
{
	DOLOG("invoking iscsi_pdu_nop_out::get_response\n");

	iscsi_response_set response;
	auto reply_pdu = new iscsi_pdu_nop_in();
	if (reply_pdu->set(*this) == false) {
		delete reply_pdu;
		return { };
	}
	response.responses.push_back(reply_pdu);

	return response;
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_nop_in::iscsi_pdu_nop_in()
{
	assert(sizeof(*nop_in) == 48);
}

iscsi_pdu_nop_in::~iscsi_pdu_nop_in()
{
}

bool iscsi_pdu_nop_in::set(const iscsi_pdu_nop_out & reply_to)
{
	*nop_in = { };
	nop_in->opcode     = o_nop_in;
	nop_in->datalenH   = 0;
	nop_in->datalenM   = 0;
	nop_in->datalenL   = 0;
	memcpy(nop_in->LUN, reply_to.get_LUN(), sizeof nop_in->LUN);
	nop_in->Itasktag   = reply_to.get_Itasktag();
	nop_in->TTF        = reply_to.get_TTF();
	nop_in->StatSN     = htonl(reply_to.get_ExpStatSN());
	nop_in->ExpCmdSN   = htonl(reply_to.get_CmdSN() + 1);
	nop_in->MaxCmdSN   = htonl(reply_to.get_CmdSN() + 1);

	return true;
}

std::pair<const uint8_t *, std::size_t> iscsi_pdu_nop_in::get()
{
	size_t out_size = sizeof *nop_in;
	uint8_t *out = new uint8_t[out_size];
	memcpy(out, nop_in, sizeof *nop_in);

	return { out, out_size };
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_scsi_r2t::iscsi_pdu_scsi_r2t()
{
	assert(sizeof(*pdu_scsi_r2t) == 48);
}

iscsi_pdu_scsi_r2t::~iscsi_pdu_scsi_r2t()
{
}

bool iscsi_pdu_scsi_r2t::set(const iscsi_pdu_scsi_cmd & reply_to, const uint32_t buffer_offset, const uint32_t data_length)
{
	*pdu_scsi_r2t = { };
	pdu_scsi_r2t->opcode     = o_r2t;
	pdu_scsi_r2t->datalenH   = 0;
	pdu_scsi_r2t->datalenM   = 0;
	pdu_scsi_r2t->datalenL   = 0;
	memcpy(pdu_scsi_r2t->LUN, reply_to.get_LUN(), sizeof pdu_scsi_r2t->LUN);
	pdu_scsi_r2t->Itasktag   = reply_to.get_Itasktag();
	// TODO pdu_scsi_r2t->TTF        = reply_to.get_TTF();
	pdu_scsi_r2t->StatSN     = htonl(reply_to.get_ExpStatSN());
	pdu_scsi_r2t->ExpCmdSN   = htonl(reply_to.get_CmdSN() + 1);
	pdu_scsi_r2t->MaxCmdSN   = htonl(reply_to.get_CmdSN() + 1);
	pdu_scsi_r2t->bufferoff  = buffer_offset;
	pdu_scsi_r2t->DDTF       = data_length;

	return true;
}

std::pair<const uint8_t *, std::size_t> iscsi_pdu_scsi_r2t::get()
{
	size_t out_size = sizeof *pdu_scsi_r2t;
	uint8_t *out = new uint8_t[out_size];
	memcpy(out, pdu_scsi_r2t, sizeof *pdu_scsi_r2t);

	return { out, out_size };
}
