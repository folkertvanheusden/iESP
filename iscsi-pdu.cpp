#include <cassert>
#include <cstring>
#include <vector>
#include <arpa/inet.h>

#include "iscsi.h"
#include "iscsi-pdu.h"
#include "log.h"
#include "random.h"
#include "scsi.h"
#include "utils.h"


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

	delete [] data.first;
}

std::vector<blob_t> iscsi_pdu_bhs::return_helper(void *const data, size_t n)
{
	std::vector<blob_t> out;
	out.push_back({ reinterpret_cast<uint8_t *>(data), n });
	return out;
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

		if (ahs_length > 0) {
			iscsi_pdu_ahs *instance = new iscsi_pdu_ahs();
			if (instance->set(&ahs_in.first[offset], ahs_length) == false) {
				delete instance;
				break;
			}

			ahs_list.push_back(instance);
		}
	}

	return offset == ahs_in.second;
}

bool iscsi_pdu_bhs::set_data(std::pair<const uint8_t *, std::size_t> data_in)
{
	if (data_in.second == 0 || data_in.second > 16777215)
		return false;

	data.second = data_in.second;

	data.first  = new uint8_t[data_in.second]();
	memcpy(data.first, data_in.first, data_in.second);

	return true;
}

std::optional<std::pair<uint8_t *, size_t> > iscsi_pdu_bhs::get_data() const
{
	if (data.second == 0)
		return { };

	uint8_t *out = new uint8_t[data.second]();
	memcpy(out, data.first, data.second);

	return { { out, data.second } };
}

std::vector<blob_t> iscsi_pdu_bhs::get()
{
	void *out = new uint8_t[sizeof *bhs]();
	memcpy(out, bhs, sizeof *bhs);

	return return_helper(out, sizeof *bhs);
}

std::optional<iscsi_response_set> iscsi_pdu_bhs::get_response(session *const s, const iscsi_response_parameters *const parameters_in)
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

	ahs = reinterpret_cast<__ahs_header__ *>(new uint8_t[expected_size + 3]());
	memcpy(ahs, in, n);

	return true;
}

blob_t iscsi_pdu_ahs::get()
{
	uint16_t expected_size = ntohs(reinterpret_cast<const __ahs_header__ *>(ahs)->length);
	uint32_t out_size      = sizeof(__ahs_header__) + expected_size;
	uint8_t *out = new uint8_t[out_size]();
	memcpy(out, ahs, out_size);

	return { out, out_size };
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

std::vector<blob_t> iscsi_pdu_login_request::get()
{
	void *out = new uint8_t[sizeof *login_req]();
	memcpy(out, login_req, sizeof *login_req);

	return return_helper(out, sizeof *login_req);
}

std::optional<iscsi_response_set> iscsi_pdu_login_request::get_response(session *const s, const iscsi_response_parameters *const parameters_in)
{
	auto parameters = static_cast<const iscsi_response_parameters_login_req *>(parameters_in);

	auto kvs_in = data_to_text_array(data.first, data.second);
	uint32_t max_burst = ~0;
	for(auto & kv: kvs_in) {
		auto parts = split(kv, "=");
		if (parts.size() < 2)
			continue;

		if (parts[0] == "MaxBurstLength")
			max_burst = std::min(max_burst, uint32_t(std::atoi(parts[1].c_str())));
		if (parts[0] == "FirstBurstLength")
			max_burst = std::min(max_burst, uint32_t(std::atoi(parts[1].c_str())));
	}

	if (max_burst < uint32_t(~0)) {
		DOLOG("iscsi_pdu_login_request::get_response: set max-burst to %u\n", max_burst);
		s->set_ack_interval(max_burst);
	}

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
	delete [] login_reply_reply_data.first;
}

bool iscsi_pdu_login_reply::set(const iscsi_pdu_login_request & reply_to)
{
	const std::vector<std::string> kvs {
		"TargetPortalGroupTag=1",
		"HeaderDigest=None",
		"DataDigest=None",
		"DefaultTime2Wait=1",
		"DefaultTime2Retain=0",
		"IFMarker=No",
		"OFMarker=No",
//		"AuthMethod=None",
		"ErrorRecoveryLevel=0",
		"MaxConnections=1",
//		"TargetName=iqn.1993-11.com.vanheusden:test",
//		"TargetAlias=Bob-s disk",  // TODO
		"ImmediateData=Yes",
		"MaxRecvDataSegmentLength=1024",
		"MaxBurstLength=512",
		"FirstBurstLength=512",
		"TargetPortalGroupTag=1",
		"InitialR2T=No",
		"MaxOutstandingR2T=1",
		"DataPDUInOrder=Yes",
		"DataSequenceInOrder=Yes",
	};
	auto temp = text_array_to_data(kvs);
	login_reply_reply_data.first  = temp.first;
	login_reply_reply_data.second = temp.second;

	*login_reply = { };
	set_bits(&login_reply->b1, 7, 1, false);  // filler 1
	set_bits(&login_reply->b1, 6, 1, false);  // filler 0
	set_bits(&login_reply->b1, 0, 6, o_login_resp);  // opcode

	set_bits(&login_reply->b2, 7, 1, true);  // T
	set_bits(&login_reply->b2, 6, 1, false);  // C
	set_bits(&login_reply->b2, 2, 2, reply_to.get_CSG());  // CSG
	set_bits(&login_reply->b2, 0, 2, reply_to.get_NSG());  // NSG

	login_reply->versionmax = reply_to.get_versionmin();
	login_reply->versionact = reply_to.get_versionmin();
	login_reply->ahslen     = 0;
	login_reply->datalenH   = login_reply_reply_data.second >> 16;
	login_reply->datalenM   = login_reply_reply_data.second >>  8;
	login_reply->datalenL   = login_reply_reply_data.second      ;
	memcpy(login_reply->ISID, reply_to.get_ISID(), 6);
	do {
		my_getrandom(&login_reply->TSIH, sizeof login_reply->TSIH);
	}
	while(login_reply->TSIH == 0);
	login_reply->Itasktag   = reply_to.get_Itasktag();
	login_reply->StatSN     = htonl(reply_to.get_ExpStatSN());
	login_reply->ExpCmdSN   = htonl(reply_to.get_CmdSN());
	login_reply->MaxStatSN  = htonl(reply_to.get_ExpStatSN());

	return true;
}

std::vector<blob_t> iscsi_pdu_login_reply::get()
{
	// round for padding
	size_t data_size_padded = (login_reply_reply_data.second + 3) & ~3;

	size_t out_size = sizeof(*login_reply) + data_size_padded;
	uint8_t *out = new uint8_t[out_size]();
	memcpy(out, login_reply, sizeof *login_reply);
	memcpy(&out[sizeof *login_reply], login_reply_reply_data.first, login_reply_reply_data.second);

	assert((out_size & ~3) == out_size);
	return return_helper(out, out_size);
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

std::vector<blob_t> iscsi_pdu_scsi_cmd::get()
{
	size_t out_size = sizeof *cdb_pdu_req;
	void *out = new uint8_t[out_size]();
	memcpy(out, cdb_pdu_req, sizeof *cdb_pdu_req);

	return return_helper(out, out_size);
}

std::optional<iscsi_response_set> iscsi_pdu_scsi_cmd::get_response(session *const s, const iscsi_response_parameters *const parameters_in, const uint8_t status)
{
	// TODO: handle errors (see 'status')
	iscsi_response_set response;

	auto *pdu_scsi_response = new iscsi_pdu_scsi_response() /* 0x21 */;
	if (pdu_scsi_response->set(*this, { }, { }) == false) {
		DOLOG("iscsi_pdu_scsi_cmd::get_response: iscsi_pdu_scsi_response::set returned error\n");

		return { };
	}

	response.responses.push_back(pdu_scsi_response);

	return response;
}

std::optional<iscsi_response_set> iscsi_pdu_scsi_cmd::get_response(session *const s, const iscsi_response_parameters *const parameters_in)
{
	DOLOG("iscsi_pdu_scsi_cmd::get_response: working on ITT %08x\n", get_Itasktag());
	auto parameters = static_cast<const iscsi_response_parameters_scsi_cmd *>(parameters_in);
	auto scsi_reply = parameters->sd->send(get_CDB(), 16, data);
	if (scsi_reply.has_value() == false) {
		DOLOG("iscsi_pdu_scsi_cmd::get_response: scsi::send returned nothing\n");
		return { };
	}

	iscsi_response_set response;
	bool               ok       { true };

	if (scsi_reply.value().io.is_inline) {
		auto pdu_data_in = new iscsi_pdu_scsi_data_in();  // 0x25
		DOLOG("iscsi_pdu_scsi_cmd::get_response: sending SCSI DATA-IN with %zu payload bytes, is meta: %d\n", scsi_reply.value().io.what.data.second, scsi_reply.value().data_is_meta);
		if (pdu_data_in->set(s, *this, scsi_reply.value().io.what.data, scsi_reply.value().data_is_meta) == false) {
			ok = false;
			DOLOG("iscsi_pdu_scsi_cmd::get_response: iscsi_pdu_scsi_data_in::set returned error state\n");
		}
		response.responses.push_back(pdu_data_in);
		delete [] scsi_reply.value().io.what.data.first;
	}
	else {
		assert(scsi_reply.value().io.what.data.first == nullptr);
	}

	iscsi_pdu_bhs *pdu_scsi_response = nullptr;
	if (scsi_reply.value().type == ir_as_is || scsi_reply.value().type == ir_empty_sense) {
		if (scsi_reply.value().sense_data.empty() == false || scsi_reply.value().type == ir_empty_sense) {
			auto *temp = new iscsi_pdu_scsi_response() /* 0x21 */;
			DOLOG("iscsi_pdu_scsi_cmd::get_response: sending SCSI response with %zu sense bytes\n", scsi_reply.value().sense_data.size());

			if (temp->set(*this, scsi_reply.value().sense_data, { }) == false) {
				ok = false;
				DOLOG("iscsi_pdu_scsi_cmd::get_response: iscsi_pdu_scsi_response::set returned error\n");
			}
			pdu_scsi_response = temp;
		}
	}
	else if (scsi_reply.value().type == ir_r2t) {
		auto *temp = new iscsi_pdu_scsi_r2t() /* 0x31 */;
		DOLOG("iscsi_pdu_scsi_cmd::get_response: sending R2T with %zu sense bytes\n", scsi_reply.value().sense_data.size());
		uint32_t TTT = s->init_r2t_session(scsi_reply.value().r2t, this);
		DOLOG("iscsi_pdu_scsi_cmd::get_response: TTT is %08x\n", TTT);
		if (temp->set(s, *this, TTT, scsi_reply.value().r2t.bytes_done, scsi_reply.value().r2t.bytes_left) == false) {
			ok = false;
			DOLOG("iscsi_pdu_scsi_cmd::get_response: iscsi_pdu_scsi_response::set returned error\n");
		}
		pdu_scsi_response = temp;
	}
	if (pdu_scsi_response)
		response.responses.push_back(pdu_scsi_response);

	if (!ok) {
		for(auto & r: response.responses)
			delete r;

		DOLOG("iscsi_pdu_scsi_cmd::get_response: failed\n");

		return { };
	}

	if (scsi_reply.value().io.is_inline == false) {
		DOLOG("iscsi_pdu_scsi_cmd::get_response: queing stream\n");

		response.to_stream = scsi_reply.value().io.what.location;
	}

	return response;
}

blob_t iscsi_pdu_scsi_cmd::get_raw() const
{
	size_t copy_len = sizeof *cdb_pdu_req;
	uint8_t *copy_data = new uint8_t[copy_len]();
	memcpy(copy_data, cdb_pdu_req, sizeof *cdb_pdu_req);

	return { copy_data, copy_len };
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_scsi_response::iscsi_pdu_scsi_response()
{
	assert(sizeof(*pdu_response) == 48);
}

iscsi_pdu_scsi_response::~iscsi_pdu_scsi_response()
{
	delete [] pdu_response_data.first;
}

bool iscsi_pdu_scsi_response::set(const iscsi_pdu_scsi_cmd & reply_to, const std::vector<uint8_t> & scsi_sense_data, std::optional<uint32_t> ResidualCt)
{
	size_t sense_data_size = scsi_sense_data.size();
	size_t reply_data_plus_sense_header = sense_data_size > 0 ? 2 + sense_data_size : 0;

	*pdu_response = { };
	set_bits(&pdu_response->b1, 0, 6, o_scsi_resp);  // 0x21
	set_bits(&pdu_response->b1, 6, 1, false);
	set_bits(&pdu_response->b1, 7, 1, false);

	set_bits(&pdu_response->b2, 7, 1, true);

	pdu_response->datalenH   = reply_data_plus_sense_header >> 16;
	pdu_response->datalenM   = reply_data_plus_sense_header >>  8;
	pdu_response->datalenL   = reply_data_plus_sense_header      ;
	pdu_response->Itasktag   = reply_to.get_Itasktag();
	pdu_response->StatSN     = htonl(reply_to.get_ExpStatSN());
	pdu_response->ExpCmdSN   = htonl(reply_to.get_CmdSN() + 1);
	pdu_response->MaxCmdSN   = htonl(reply_to.get_CmdSN() + 1);
	pdu_response->ExpDataSN  = htonl(0);
	if (ResidualCt.has_value()) {
		set_bits(&pdu_response->b2, 1, 1, true);  // U (residual underflow)
		pdu_response->ResidualCt = ResidualCt.value();
	}

	pdu_response_data.second = reply_data_plus_sense_header;
	if (pdu_response_data.second) {
		DOLOG("---------------- CHECK CONDITION\n");
		pdu_response->status       = 0x02;  // check condition
		pdu_response->response     = 0x01;  // target failure
		pdu_response->ExpDataSN    = 0;

		pdu_response_data.first    = new uint8_t[pdu_response_data.second]();
		pdu_response_data.first[0] = sense_data_size >> 8;
		pdu_response_data.first[1] = sense_data_size;
		memcpy(pdu_response_data.first + 2, scsi_sense_data.data(), sense_data_size);
	}

	return true;
}

std::vector<blob_t> iscsi_pdu_scsi_response::get()
{
	size_t out_size = sizeof(*pdu_response) + pdu_response_data.second;
	out_size = (out_size + 3) & ~3;
	uint8_t *out = new uint8_t[out_size]();
	memcpy(out, pdu_response, sizeof *pdu_response);
	if (pdu_response_data.second)
		memcpy(&out[sizeof *pdu_response], pdu_response_data.first, pdu_response_data.second);

	return return_helper(out, out_size);
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_scsi_data_in::iscsi_pdu_scsi_data_in()
{
	assert(sizeof(*pdu_data_in) == 48);
}

iscsi_pdu_scsi_data_in::~iscsi_pdu_scsi_data_in()
{
	delete [] pdu_data_in_data.first;
}

bool iscsi_pdu_scsi_data_in::set(session *const s, const iscsi_pdu_scsi_cmd & reply_to, const std::pair<uint8_t *, size_t> scsi_reply_data, const bool has_sense)
{
	DOLOG("iscsi_pdu_scsi_data_in::set: with %zu payload bytes, has_sense: %d\n", scsi_reply_data.second, has_sense);

	this->s = s;

	auto temp = reply_to.get_raw();
	reply_to_copy.set(s, temp.data, temp.n);
	delete [] temp.data;

	pdu_data_in_data.second = scsi_reply_data.second;
	if (pdu_data_in_data.second) {
		pdu_data_in_data.first = new uint8_t[pdu_data_in_data.second]();
		memcpy(pdu_data_in_data.first, scsi_reply_data.first, pdu_data_in_data.second);
	}

	return true;
}

std::vector<blob_t> iscsi_pdu_scsi_data_in::get()
{
	std::vector<blob_t> v_out;

	// resize to limit
	auto use_pdu_data_size = pdu_data_in_data.second;

	if (use_pdu_data_size > size_t(reply_to_copy.get_ExpDatLen()))
		DOLOG("iscsi_pdu_scsi_data_in: requested less (%zu) than wat is available (%zu)\n", size_t(reply_to_copy.get_ExpDatLen()), use_pdu_data_size);
	use_pdu_data_size = std::min(use_pdu_data_size, size_t(reply_to_copy.get_ExpDatLen()));

	size_t n_to_do = (use_pdu_data_size + 511) / 512;

	for(size_t i=0, count=0; i<use_pdu_data_size; i += 512, count++) {  // 4kB blocks
		*pdu_data_in = { };
		set_bits(&pdu_data_in->b1, 0, 6, o_scsi_data_in);  // 0x25
		bool last_block = count == n_to_do - 1;
		if (last_block) {
			set_bits(&pdu_data_in->b2, 7, 1, true);  // F
			if (pdu_data_in_data.second < reply_to_copy.get_ExpDatLen()) {
				set_bits(&pdu_data_in->b2, 1, 1, true);  // U
				pdu_data_in->ResidualCt = htonl(reply_to_copy.get_ExpDatLen() - pdu_data_in_data.second);
			}
			else if (pdu_data_in_data.second > reply_to_copy.get_ExpDatLen()) {
				set_bits(&pdu_data_in->b2, 2, 1, true);  // O
				pdu_data_in->ResidualCt = htonl(pdu_data_in_data.second - reply_to_copy.get_ExpDatLen());
			}
			set_bits(&pdu_data_in->b2, 0, 1, true);  // S
		}
		size_t cur_len = std::min(use_pdu_data_size - i, size_t(512));
		DOLOG("iscsi_pdu_scsi_data_in::get: block %zu, last_block: %d, cur_len: %zu\n", count, last_block, cur_len);
		pdu_data_in->datalenH   = cur_len >> 16;
		pdu_data_in->datalenM   = cur_len >>  8;
		pdu_data_in->datalenL   = cur_len      ;
		memcpy(pdu_data_in->LUN, reply_to_copy.get_LUN(), sizeof pdu_data_in->LUN);
		pdu_data_in->Itasktag   = reply_to_copy.get_Itasktag();
		pdu_data_in->StatSN     = htonl(reply_to_copy.get_ExpStatSN());
		pdu_data_in->ExpCmdSN   = htonl(reply_to_copy.get_CmdSN() + 1);  // TODO?
		pdu_data_in->MaxCmdSN   = htonl(reply_to_copy.get_CmdSN() + 128);  // TODO?
		pdu_data_in->DataSN     = htonl(s->get_inc_datasn(reply_to_copy.get_Itasktag()));
		pdu_data_in->bufferoff  = htonl(i);
		pdu_data_in->ResidualCt = htonl(use_pdu_data_size - i);

		size_t out_size = sizeof(*pdu_data_in) + cur_len;
		out_size = (out_size + 3) & ~3;

		uint8_t *out = new uint8_t[out_size]();
		memcpy(out, pdu_data_in, sizeof *pdu_data_in);
		memcpy(&out[sizeof(*pdu_data_in)], pdu_data_in_data.first + i, cur_len);

		v_out.push_back({ out, out_size });
	}

	return v_out;
}

blob_t iscsi_pdu_scsi_data_in::gen_data_in_pdu(session *const s, const iscsi_pdu_scsi_cmd & reply_to, const blob_t & pdu_data_in_data, const size_t use_pdu_data_size, const size_t offset_in_data)
{
	bool last_block = (offset_in_data + pdu_data_in_data.n) == use_pdu_data_size;

	if (last_block)
		DOLOG("iscsi_pdu_scsi_data_in::gen_data_in_pdu: last block\n");
	else
		DOLOG("iscsi_pdu_scsi_data_in::gen_data_in_pdu: offset %zu + %zu != %zu\n", offset_in_data, pdu_data_in_data.n, use_pdu_data_size);

	__pdu_data_in__ pdu_data_in __attribute__((packed)) { };

	set_bits(&pdu_data_in.b1, 0, 6, o_scsi_data_in);  // 0x25
	if (last_block) {
		set_bits(&pdu_data_in.b2, 7, 1, true);  // F
		if (use_pdu_data_size < reply_to.get_ExpDatLen()) {
			set_bits(&pdu_data_in.b2, 1, 1, true);  // U
		}
		else if (use_pdu_data_size > reply_to.get_ExpDatLen()) {
			set_bits(&pdu_data_in.b2, 2, 1, true);  // O
		}
		set_bits(&pdu_data_in.b2, 0, 1, true);  // S
	}
	pdu_data_in.datalenH   = pdu_data_in_data.n >> 16;
	pdu_data_in.datalenM   = pdu_data_in_data.n >>  8;
	pdu_data_in.datalenL   = pdu_data_in_data.n      ;
	memcpy(pdu_data_in.LUN, reply_to.get_LUN(), sizeof pdu_data_in.LUN);
	pdu_data_in.Itasktag   = reply_to.get_Itasktag();
	pdu_data_in.StatSN     = htonl(reply_to.get_ExpStatSN());
	pdu_data_in.ExpCmdSN   = htonl(reply_to.get_CmdSN() + 1);  // TODO?
	pdu_data_in.MaxCmdSN   = htonl(reply_to.get_CmdSN() + 128);  // TODO?
	pdu_data_in.DataSN     = htonl(s->get_inc_datasn(reply_to.get_Itasktag()));
	pdu_data_in.bufferoff  = htonl(offset_in_data);
	pdu_data_in.ResidualCt = htonl(use_pdu_data_size - offset_in_data);

	size_t out_size = sizeof(pdu_data_in) + pdu_data_in_data.n;
	out_size = (out_size + 3) & ~3;

	uint8_t *out = new uint8_t[out_size]();
	memcpy(out, &pdu_data_in, sizeof pdu_data_in);
	memcpy(&out[sizeof(pdu_data_in)], pdu_data_in_data.data, pdu_data_in_data.n);

	return { out, out_size };
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_scsi_data_out::iscsi_pdu_scsi_data_out()
{
	assert(sizeof(*pdu_data_out) == 48);
}

iscsi_pdu_scsi_data_out::~iscsi_pdu_scsi_data_out()
{
	delete [] pdu_data_out_data.first;
}

bool iscsi_pdu_scsi_data_out::set(session *const s, const iscsi_pdu_scsi_cmd & reply_to, const std::pair<uint8_t *, size_t> scsi_reply_data)
{
	DOLOG("iscsi_pdu_scsi_data_out::set: with %zu payload bytes\n", scsi_reply_data.second);

	this->s = s;

	pdu_data_out_data.second = scsi_reply_data.second;
	if (pdu_data_out_data.second) {
		pdu_data_out_data.first = new uint8_t[pdu_data_out_data.second]();
		memcpy(pdu_data_out_data.first, scsi_reply_data.first, pdu_data_out_data.second);
	}

	return true;
}

std::vector<blob_t> iscsi_pdu_scsi_data_out::get()
{
	std::vector<blob_t> out;
	return out;
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_nop_out::iscsi_pdu_nop_out()
{
	assert(sizeof(*nop_out) == 48);
}

iscsi_pdu_nop_out::~iscsi_pdu_nop_out()
{
}

std::optional<iscsi_response_set> iscsi_pdu_nop_out::get_response(session *const s, const iscsi_response_parameters *const parameters_in)
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
	set_bits(&nop_in->b1, 0, 6, o_nop_in);  // opcode
	set_bits(&nop_in->b2, 7, 1, true);  // reserved
	nop_in->datalenH   = 0;
	nop_in->datalenM   = 0;
	nop_in->datalenL   = 0;
	memcpy(nop_in->LUN, reply_to.get_LUN(), sizeof nop_in->LUN);
	nop_in->Itasktag   = reply_to.get_Itasktag();
	nop_in->TTT        = reply_to.get_TTT();
	nop_in->StatSN     = htonl(reply_to.get_ExpStatSN());
	nop_in->ExpCmdSN   = htonl(reply_to.get_CmdSN() + 1);
	nop_in->MaxCmdSN   = htonl(reply_to.get_CmdSN() + 1);

	return true;
}

std::vector<blob_t> iscsi_pdu_nop_in::get()
{
	size_t out_size = sizeof *nop_in;
	uint8_t *out = new uint8_t[out_size]();
	memcpy(out, nop_in, sizeof *nop_in);

	return return_helper(out, out_size);
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_scsi_r2t::iscsi_pdu_scsi_r2t()
{
	assert(sizeof(*pdu_scsi_r2t) == 48);
}

iscsi_pdu_scsi_r2t::~iscsi_pdu_scsi_r2t()
{
	delete [] pdu_scsi_r2t_data.first;
}

bool iscsi_pdu_scsi_r2t::set(session *const s, const iscsi_pdu_scsi_cmd & reply_to, const uint32_t TTT, const uint32_t buffer_offset, const uint32_t data_length)
{
	*pdu_scsi_r2t = { };
	set_bits(&pdu_scsi_r2t->b1, 0, 6, o_r2t);
	set_bits(&pdu_scsi_r2t->b2, 7, 1, true);
	memcpy(pdu_scsi_r2t->LUN, reply_to.get_LUN(), sizeof pdu_scsi_r2t->LUN);
	pdu_scsi_r2t->Itasktag   = reply_to.get_Itasktag();
	pdu_scsi_r2t->TTT        = TTT;
	pdu_scsi_r2t->StatSN     = htonl(reply_to.get_ExpStatSN());
	pdu_scsi_r2t->ExpCmdSN   = htonl(reply_to.get_CmdSN() + 1);
	pdu_scsi_r2t->MaxCmdSN   = htonl(reply_to.get_CmdSN() + 128);
	pdu_scsi_r2t->bufferoff  = htonl(buffer_offset);
	pdu_scsi_r2t->DDTF       = htonl(data_length);

	return true;
}

std::vector<blob_t> iscsi_pdu_scsi_r2t::get()
{
	size_t out_size = sizeof *pdu_scsi_r2t;
	uint8_t *out = new uint8_t[out_size]();
	memcpy(out, pdu_scsi_r2t, sizeof *pdu_scsi_r2t);

	return return_helper(out, out_size);
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_text_request::iscsi_pdu_text_request()
{
	assert(sizeof(*text_req) == 48);

	*text_req = { };
}

iscsi_pdu_text_request::~iscsi_pdu_text_request()
{
}

bool iscsi_pdu_text_request::set(session *const s, const uint8_t *const in, const size_t n)
{
	if (iscsi_pdu_bhs::set(s, in, n) == false)
		return false;

	// TODO further validation

	return true;
}

std::vector<blob_t> iscsi_pdu_text_request::get()
{
	size_t out_size = sizeof *text_req;
	void *out = new uint8_t[out_size]();
	memcpy(out, text_req, sizeof *text_req);

	return return_helper(out, out_size);
}

std::optional<iscsi_response_set> iscsi_pdu_text_request::get_response(session *const s, const iscsi_response_parameters *const parameters_in)
{
	iscsi_response_set response;
	auto reply_pdu = new iscsi_pdu_text_reply();
	if (reply_pdu->set(*this, parameters_in) == false) {
		delete reply_pdu;
		return { };
	}
	response.responses.push_back(reply_pdu);

	return response;
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_text_reply::iscsi_pdu_text_reply()
{
	assert(sizeof(*text_reply) == 48);
}

iscsi_pdu_text_reply::~iscsi_pdu_text_reply()
{
	delete [] text_reply_reply_data.first;
}

bool iscsi_pdu_text_reply::set(const iscsi_pdu_text_request & reply_to, const iscsi_response_parameters *const parameters)
{
#if 0
	const auto data = reply_to.get_data();
	if (data.has_value() == false)
		return false;
	auto kvs_in = data_to_text_array(data.value().first, data.value().second);
	delete [] data.value().first;
	bool send_targets = false;

	for(auto & kv: kvs_in) {
		auto parts = split(kv, "=");
		if (parts.size() < 2)
			return false;

		if (parts[0] == "SendTargets")
			send_targets = true;

		DOLOG(" text request, responding to: %s\n", kv.c_str());
	}
#endif

	auto *temp_parameters = reinterpret_cast<const iscsi_response_parameters_text_req *>(parameters);
	const std::vector<std::string> kvs {
		"TargetName=iqn.1993-11.com.vanheusden:test",  // FIXME
		"TargetAddress=" + temp_parameters->listen_ip + myformat(":%d", temp_parameters->listen_port),
	};
	auto temp = text_array_to_data(kvs);
	text_reply_reply_data.first  = temp.first;
	text_reply_reply_data.second = temp.second;

	*text_reply = { };
	set_bits(&text_reply->b1, 0, 6, o_text_resp);  // opcode, 0x24
	set_bits(&text_reply->b2, 7, 1, true);  // F
	text_reply->ahslen     = 0;
	text_reply->datalenH   = text_reply_reply_data.second >> 16;
	text_reply->datalenM   = text_reply_reply_data.second >>  8;
	text_reply->datalenL   = text_reply_reply_data.second      ;
	memcpy(text_reply->LUN, reply_to.get_LUN(), sizeof text_reply->LUN);
	text_reply->TTT        = reply_to.get_TTT();
	text_reply->Itasktag   = reply_to.get_Itasktag();
	text_reply->StatSN     = htonl(reply_to.get_ExpStatSN());
	text_reply->ExpCmdSN   = htonl(reply_to.get_CmdSN());

	return true;
}

std::vector<blob_t> iscsi_pdu_text_reply::get()
{
	// round for padding
	size_t data_size_padded = (text_reply_reply_data.second + 3) & ~3;

	size_t out_size = sizeof(*text_reply) + data_size_padded;
	uint8_t *out = new uint8_t[out_size]();
	memcpy(out, text_reply, sizeof *text_reply);
	memcpy(&out[sizeof *text_reply], text_reply_reply_data.first, text_reply_reply_data.second);

	return { { out, out_size } };
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_logout_request::iscsi_pdu_logout_request()
{
	assert(sizeof(*logout_req) == 48);

	*logout_req = { };
}

iscsi_pdu_logout_request::~iscsi_pdu_logout_request()
{
}

bool iscsi_pdu_logout_request::set(session *const s, const uint8_t *const in, const size_t n)
{
	if (iscsi_pdu_bhs::set(s, in, n) == false)
		return false;

	// TODO further validation

	return true;
}

std::vector<blob_t> iscsi_pdu_logout_request::get()
{
	size_t out_size = sizeof *logout_req;
	void *out = new uint8_t[out_size]();
	memcpy(out, logout_req, sizeof *logout_req);

	return return_helper(out, out_size);
}

std::optional<iscsi_response_set> iscsi_pdu_logout_request::get_response(session *const s, const iscsi_response_parameters *const parameters_in)
{
	auto parameters = static_cast<const iscsi_response_parameters_logout_req *>(parameters_in);

	iscsi_response_set response;
	auto reply_pdu = new iscsi_pdu_logout_reply();
	if (reply_pdu->set(*this) == false) {
		delete reply_pdu;
		return { };
	}
	response.responses.push_back(reply_pdu);

	return response;
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_logout_reply::iscsi_pdu_logout_reply()
{
	assert(sizeof(*logout_reply) == 48);
}

iscsi_pdu_logout_reply::~iscsi_pdu_logout_reply()
{
	delete [] logout_reply_reply_data.first;
}

bool iscsi_pdu_logout_reply::set(const iscsi_pdu_logout_request & reply_to)
{
	*logout_reply = { };
	set_bits(&logout_reply->b1, 0, 6, o_logout_resp);  // 0x26
	set_bits(&logout_reply->b2, 7, 1, true);
	logout_reply->Itasktag = reply_to.get_Itasktag();
	logout_reply->StatSN   = htonl(reply_to.get_ExpStatSN());
	logout_reply->ExpCmdSN = htonl(reply_to.get_CmdSN());
	logout_reply->MaxCmdSN = htonl(reply_to.get_CmdSN());

	return true;
}

std::vector<blob_t> iscsi_pdu_logout_reply::get()
{
	size_t   out_size = sizeof(*logout_reply);
	uint8_t *out      = new uint8_t[out_size]();
	memcpy(out, logout_reply, sizeof *logout_reply);

	return { { out, out_size } };
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_taskman_request::iscsi_pdu_taskman_request()
{
	assert(sizeof(*taskman_req) == 48);

	*taskman_req = { };
}

iscsi_pdu_taskman_request::~iscsi_pdu_taskman_request()
{
}

bool iscsi_pdu_taskman_request::set(session *const s, const uint8_t *const in, const size_t n)
{
	if (iscsi_pdu_bhs::set(s, in, n) == false)
		return false;

	// TODO further validation

	return true;
}

std::vector<blob_t> iscsi_pdu_taskman_request::get()
{
	size_t out_size = sizeof *taskman_req;
	void *out = new uint8_t[out_size]();
	memcpy(out, taskman_req, sizeof *taskman_req);

	return return_helper(out, out_size);
}

std::optional<iscsi_response_set> iscsi_pdu_taskman_request::get_response(session *const s, const iscsi_response_parameters *const parameters_in)
{
	auto parameters = static_cast<const iscsi_response_parameters_taskman *>(parameters_in);

	iscsi_response_set response;
	auto reply_pdu = new iscsi_pdu_taskman_reply();
	if (reply_pdu->set(*this) == false) {
		delete reply_pdu;
		return { };
	}
	response.responses.push_back(reply_pdu);

	return response;
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_taskman_reply::iscsi_pdu_taskman_reply()
{
	assert(sizeof(*taskman_reply) == 48);
}

iscsi_pdu_taskman_reply::~iscsi_pdu_taskman_reply()
{
}

bool iscsi_pdu_taskman_reply::set(const iscsi_pdu_taskman_request & reply_to)
{
	*taskman_reply = { };
	set_bits(&taskman_reply->b1, 0, 6, o_scsi_taskmanr);  // 0x22
	set_bits(&taskman_reply->b2, 7, 1, true);
	taskman_reply->Itasktag = reply_to.get_Itasktag();
	taskman_reply->StatSN   = htonl(reply_to.get_ExpStatSN());
	taskman_reply->ExpCmdSN = htonl(reply_to.get_CmdSN() + 1);
	taskman_reply->MaxCmdSN = htonl(reply_to.get_CmdSN() + 1);

	return true;
}

std::vector<blob_t> iscsi_pdu_taskman_reply::get()
{
	size_t   out_size = sizeof(*taskman_reply);
	uint8_t *out      = new uint8_t[out_size]();
	memcpy(out, taskman_reply, sizeof *taskman_reply);

	return { { out, out_size } };
}
