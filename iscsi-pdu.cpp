#include <cassert>
#include <cstring>
#include <vector>

#include "iscsi.h"
#include "iscsi-pdu.h"
#include "log.h"
#include "random.h"
#include "scsi.h"
#include "utils.h"


constexpr int max_msg_depth = 128;

std::optional<std::string> pdu_opcode_to_string(const iscsi_pdu_bhs::iscsi_bhs_opcode opcode)
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

	return { };
}

/*--------------------------------------------------------------------------*/
// BHS

iscsi_pdu_bhs::iscsi_pdu_bhs(session *const ses): ses(ses)
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

std::vector<blob_t> iscsi_pdu_bhs::get_helper(const void *const header, const uint8_t *const data, const size_t data_len, const bool allow_digest) const
{
	size_t                  header_size   = sizeof(__bhs__);
	assert(header_size == 48);
	size_t                  out_size      = header_size;
	std::optional<uint32_t> header_digest;
	size_t                  header_digest_offset = header_size;
	assert(header_digest_offset == 48);
	size_t                  data_offset          = 0;
	size_t                  data_digest_offset   = 0;
	size_t                  data_padded_length   = 0;
	size_t                  digest_length = sizeof(uint32_t);

	if (ses->get_header_digest() && allow_digest) {
		uint32_t crc32 = crc32_0x11EDC6F41(reinterpret_cast<const uint8_t *>(header), header_size, { }).first;
		out_size += digest_length;
		header_digest = crc32;
	}

	data_offset        = out_size;
	data_padded_length = (data_len + 3) & ~3;
	out_size          += data_padded_length;
	data_digest_offset = out_size;
	if (ses->get_data_digest() && allow_digest && data_len > 0)
		out_size += digest_length;

	uint8_t *out = new uint8_t[out_size];

	memcpy(&out[0], header, header_size);
	if (header_digest.has_value())
		memcpy(&out[header_digest_offset], &header_digest.value(), digest_length);
	if (data_len > 0) {
		if (ses->get_data_digest() && allow_digest) {
			memset(&out[out_size] - (4 /* data padding */ + digest_length), 0x00, 4);  // make sure padding is 0x00
			memcpy(&out[data_offset], data, data_len);

			uint32_t data_digest = crc32_0x11EDC6F41(&out[data_offset], data_padded_length, { }).first;
			memcpy(&out[data_digest_offset], &data_digest, digest_length);
		}
		else {
			memset(&out[out_size] - 4 /* data padding */, 0x00, 4);
			memcpy(&out[data_offset], data, data_len);
		}
	}

	std::vector<blob_t> out_vector;
	out_vector.push_back({ out, out_size });
	return out_vector;
}

bool iscsi_pdu_bhs::set(const uint8_t *const in, const size_t n)
{
	if (n != 48)
		return false;

	memcpy(pdu_bytes, in, n);

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

	delete [] data.first;
	data.second = data_in.second;
	data.first  = duplicate_new(data_in.first, data_in.second);

	return true;
}

std::optional<std::pair<const uint8_t *, size_t> > iscsi_pdu_bhs::get_data() const
{
	if (data.second == 0)
		return { };

	return { { data.first, data.second } };
}

std::vector<blob_t> iscsi_pdu_bhs::get() const
{
	return get_helper(bhs, nullptr, 0);
}

std::optional<iscsi_response_set> iscsi_pdu_bhs::get_response(scsi *const sd)
{
	DOLOG(logging::ll_debug, "iscsi_pdu_bhs::get_response", ses->get_endpoint_name(), "invoked!");
	assert(0);
	return { };
}

blob_t iscsi_pdu_bhs::get_raw() const
{
	size_t copy_len    = sizeof pdu_bytes;
	assert(copy_len == 48);
	uint8_t *copy_data = duplicate_new(pdu_bytes, copy_len);

	return { copy_data, copy_len };
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

	size_t expected_size = my_NTOHS(reinterpret_cast<const __ahs_header__ *>(in)->length);
	if (expected_size + 3 != n)
		return false;

	ahs = reinterpret_cast<__ahs_header__ *>(new uint8_t[expected_size + 3]());
	memcpy(ahs, in, n);

	return true;
}

blob_t iscsi_pdu_ahs::get()
{
	uint16_t expected_size = my_NTOHS(reinterpret_cast<const __ahs_header__ *>(ahs)->length);
	uint32_t out_size      = sizeof(__ahs_header__) + expected_size;
	uint8_t *out           = duplicate_new(ahs, out_size);

	return { out, out_size };
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_login_request::iscsi_pdu_login_request(session *const ses): iscsi_pdu_bhs(ses)
{
	assert(sizeof(*login_req) == 48);

	*login_req = { };
}

iscsi_pdu_login_request::~iscsi_pdu_login_request()
{
}

bool has_CRC32C(const std::string & value)
{
	auto parts = split(value, ",");
	for(auto & part: parts) {
		if (part == "CRC32C")
			return true;
	}

	return false;
}

bool iscsi_pdu_login_request::set_data(std::pair<const uint8_t *, std::size_t> data_in)
{
	if (iscsi_pdu_bhs::set_data(data_in) == false) {
		DOLOG(logging::ll_warning, "iscsi_pdu_login_request::set_data", ses->get_endpoint_name(), "iscsi_pdu_bhs::set_data returned false");
		return false;
	}

	auto        kvs_in      = data_to_text_array(data.first, data.second);
	uint32_t    max_burst   = ~0;
	std::string target_name;
	bool        discovery   = false;
	for(auto & kv: kvs_in) {
		DOLOG(logging::ll_debug, "iscsi_pdu_login_request::set_data", ses->get_endpoint_name(), "kv %s", kv.c_str());

		auto parts = split(kv, "=");
		if (parts.size() < 2)
			continue;

		if (parts[0] == "MaxBurstLength")
			max_burst = std::min(max_burst, uint32_t(std::stoi(parts[1])));
		else if (parts[0] == "FirstBurstLength")
			max_burst = std::min(max_burst, uint32_t(std::stoi(parts[1])));
		else if (parts[0] == "InitiatorName")
			initiator = parts[1];
		else if (parts[0] == "TargetName")
			target_name = parts[1];
		else if (parts[0] == "SessionType")
			discovery = parts[1] == "Discovery";
		else if (parts[0] == "HeaderDigest")
			ses->set_header_digest(has_CRC32C(parts[1]));
		else if (parts[0] == "DataDigest")
			ses->set_data_digest(has_CRC32C(parts[1]));
	}

	if (max_burst < uint32_t(~0)) {
		DOLOG(logging::ll_debug, "iscsi_pdu_login_request::set_data", ses->get_endpoint_name(), "set max-burst to %u", max_burst);
		ses->set_ack_interval(max_burst);
	}

	if (target_name != ses->get_target_name() && discovery == false) {
		DOLOG(logging::ll_warning, "iscsi_pdu_login_request::set_data", ses->get_endpoint_name(), "invalid target name \"%s\", expecting \"%s\"", target_name.c_str(), ses->get_target_name().c_str());
		return false;
	}

	return true;
}

std::vector<blob_t> iscsi_pdu_login_request::get() const
{
	return get_helper(login_req, nullptr, 0, false);
}

std::optional<iscsi_response_set> iscsi_pdu_login_request::get_response(scsi *const sd)
{
	iscsi_response_set response;
	auto reply_pdu = new iscsi_pdu_login_reply(ses);
	if (reply_pdu->set(*this) == false) {
		delete reply_pdu;
		return { };
	}
	response.responses.push_back(reply_pdu);

	return response;
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_login_reply::iscsi_pdu_login_reply(session *const ses): iscsi_pdu_bhs(ses)
{
	assert(sizeof(*login_reply) == 48);
}

iscsi_pdu_login_reply::~iscsi_pdu_login_reply()
{
	delete [] login_reply_reply_data.first;
}

bool iscsi_pdu_login_reply::set(const iscsi_pdu_login_request & reply_to)
{
	bool discovery = reply_to.get_NSG() == 1;

	if (discovery) {
		DOLOG(logging::ll_debug, "iscsi_pdu_login_reply::set", ses->get_endpoint_name(), "discovery mode");

		const std::vector<std::string> kvs {
			"TargetPortalGroupTag=1",
			"AuthMethod=None",
		};
		for(auto & kv : kvs)
			DOLOG(logging::ll_debug, "iscsi_pdu_login_reply::set", ses->get_endpoint_name(), "send KV \"%s\"", kv.c_str());
		auto temp = text_array_to_data(kvs);
		login_reply_reply_data.first  = temp.first;
		login_reply_reply_data.second = temp.second;
	}
	else {
		DOLOG(logging::ll_debug, "iscsi_pdu_login_reply::set", ses->get_endpoint_name(), "login mode");

		const std::vector<std::string> kvs {
			ses->get_header_digest() ? "HeaderDigest=CRC32C" : "HeaderDigest=None",
			ses->get_data_digest  () ? "DataDigest=CRC32C"   : "DataDigest=None",
			"DefaultTime2Wait=2",
			"DefaultTime2Retain=20",
			"ErrorRecoveryLevel=0",
			myformat("MaxRecvDataSegmentLength=%u", MAX_DATA_SEGMENT_SIZE),
		};
		for(auto & kv : kvs)
			DOLOG(logging::ll_debug, "iscsi_pdu_login_reply::set", ses->get_endpoint_name(), "send KV \"%s\"", kv.c_str());
		auto temp = text_array_to_data(kvs);
		login_reply_reply_data.first  = temp.first;
		login_reply_reply_data.second = temp.second;
	}

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
	login_reply->datalenH   = login_reply_reply_data.second >> 16;
	login_reply->datalenM   = login_reply_reply_data.second >>  8;
	login_reply->datalenL   = login_reply_reply_data.second      ;
	memcpy(login_reply->ISID, reply_to.get_ISID(), 6);
	if (!discovery) {
		do {
			if (my_getrandom(&login_reply->TSIH, sizeof login_reply->TSIH) == false) {
				DOLOG(logging::ll_error, "iscsi_pdu_login_reply::set", ses->get_endpoint_name(), "random generator returned an error");
				return false;
			}
		}
		while(login_reply->TSIH == 0);
	}
	login_reply->Itasktag   = reply_to.get_Itasktag();
	login_reply->StatSN     = my_HTONL(discovery ? 0 : reply_to.get_ExpStatSN());
	login_reply->ExpCmdSN   = my_HTONL(reply_to.get_CmdSN());
	login_reply->MaxCmdSN   = my_HTONL(reply_to.get_CmdSN() + 1);

	return true;
}

std::vector<blob_t> iscsi_pdu_login_reply::get() const
{
	return get_helper(login_reply, login_reply_reply_data.first, login_reply_reply_data.second, false);
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_scsi_cmd::iscsi_pdu_scsi_cmd(session *const ses): iscsi_pdu_bhs(ses)
{
	assert(sizeof(*cdb_pdu_req) == 48);
	assert(offsetof(__cdb_pdu_req__, Itasktag ) == 16);
	assert(offsetof(__cdb_pdu_req__, expdatlen) == 20);
	assert(offsetof(__cdb_pdu_req__, CmdSN    ) == 24);
}

iscsi_pdu_scsi_cmd::~iscsi_pdu_scsi_cmd()
{
}

bool iscsi_pdu_scsi_cmd::set(const uint8_t *const in, const size_t n)
{
	if (iscsi_pdu_bhs::set(in, n) == false) {
		DOLOG(logging::ll_info, "iscsi_pdu_scsi_cmd::set", ses->get_endpoint_name(), "iscsi_pdu_bhs::set returned error state");
		return false;
	}

	// TODO further validation

	return true;
}

std::vector<blob_t> iscsi_pdu_scsi_cmd::get() const
{
	return get_helper(cdb_pdu_req, nullptr, 0);
}

std::optional<iscsi_response_set> iscsi_pdu_scsi_cmd::get_response(scsi *const sd, const uint8_t status)
{
	// TODO: handle errors (see 'status')
	iscsi_response_set response;

	auto *pdu_scsi_response = new iscsi_pdu_scsi_response(ses) /* 0x21 */;
	if (pdu_scsi_response->set(*this, { }, { }) == false) {
		DOLOG(logging::ll_info, "iscsi_pdu_scsi_cmd::get_response", ses->get_endpoint_name(), "iscsi_pdu_scsi_response::set returned error");

		return { };
	}

	response.responses.push_back(pdu_scsi_response);

	return response;
}

std::optional<iscsi_response_set> iscsi_pdu_scsi_cmd::get_response(scsi *const sd)
{
	const uint64_t lun = get_LUN_nr();	
	DOLOG(logging::ll_debug, "iscsi_pdu_scsi_cmd::get_response", ses->get_endpoint_name(), "working on ITT %08x for LUN %" PRIu64, get_Itasktag(), lun);

	auto scsi_reply = sd->send(lun, get_CDB(), 16, data);
	if (scsi_reply.has_value() == false) {
		DOLOG(logging::ll_warning, "iscsi_pdu_scsi_cmd::get_response", ses->get_endpoint_name(), "scsi::send returned nothing");
		return { };
	}

	iscsi_response_set response;
	bool               ok       { true };

	if (scsi_reply.value().io.is_inline) {
		auto pdu_data_in = new iscsi_pdu_scsi_data_in(ses);  // 0x25
		DOLOG(logging::ll_debug, "iscsi_pdu_scsi_cmd::get_response", ses->get_endpoint_name(), "sending SCSI DATA-IN with %zu payload bytes, is meta: %d", scsi_reply.value().io.what.data.second, scsi_reply.value().data_is_meta);

		if (pdu_data_in->set(*this, scsi_reply.value().io.what.data, scsi_reply.value().data_is_meta) == false) {
			ok = false;
			DOLOG(logging::ll_error, "iscsi_pdu_scsi_cmd::get_response", ses->get_endpoint_name(), "iscsi_pdu_scsi_data_in::set returned error state");
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
			auto *temp = new iscsi_pdu_scsi_response(ses) /* 0x21 */;
			DOLOG(logging::ll_debug, "iscsi_pdu_scsi_cmd::get_response", ses->get_endpoint_name(), "sending SCSI response with %zu sense bytes", scsi_reply.value().sense_data.size());

			if (temp->set(*this, scsi_reply.value().sense_data, { }) == false) {
				ok = false;
				DOLOG(logging::ll_info, "iscsi_pdu_scsi_cmd::get_response", ses->get_endpoint_name(), "iscsi_pdu_scsi_response::set returned error");
			}
			pdu_scsi_response = temp;
		}
	}
	else if (scsi_reply.value().type == ir_r2t) {
		auto *temp = new iscsi_pdu_scsi_r2t(ses) /* 0x31 */;
		DOLOG(logging::ll_debug, "iscsi_pdu_scsi_cmd::get_response", ses->get_endpoint_name(), "sending R2T with %zu sense bytes", scsi_reply.value().sense_data.size());

		uint32_t TTT = ses->init_r2t_session(scsi_reply.value().r2t, scsi_reply.value().fua, this);
		DOLOG(logging::ll_debug, "iscsi_pdu_scsi_cmd::get_response", ses->get_endpoint_name(), "TTT is %08x", TTT);

		if (temp->set(*this, TTT, scsi_reply.value().r2t.bytes_done, scsi_reply.value().r2t.bytes_left) == false) {
			ok = false;
			DOLOG(logging::ll_info, "iscsi_pdu_scsi_cmd::get_response", ses->get_endpoint_name(), "iscsi_pdu_scsi_response::set returned error");
		}
		pdu_scsi_response = temp;
	}
	if (pdu_scsi_response)
		response.responses.push_back(pdu_scsi_response);

	if (!ok) {
		for(auto & r: response.responses)
			delete r;

		DOLOG(logging::ll_info, "iscsi_pdu_scsi_cmd::get_response", ses->get_endpoint_name(), "failed");

		return { };
	}

	if (scsi_reply.value().io.is_inline == false && scsi_reply.value().io.what.location.n_sectors > 0) {
		DOLOG(logging::ll_debug, "iscsi_pdu_scsi_cmd::get_response", ses->get_endpoint_name(), "queing stream");

		response.to_stream = scsi_reply.value().io.what.location;
	}

	return response;
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_scsi_response::iscsi_pdu_scsi_response(session *const ses): iscsi_pdu_bhs(ses)
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
	pdu_response->StatSN     = my_HTONL(reply_to.get_ExpStatSN());
	pdu_response->ExpCmdSN   = my_HTONL(reply_to.get_CmdSN() + 1);
	pdu_response->MaxCmdSN   = my_HTONL(reply_to.get_CmdSN() + max_msg_depth);
	pdu_response->ExpDataSN  = my_HTONL(0);
	if (ResidualCt.has_value()) {
		set_bits(&pdu_response->b2, 1, 1, true);  // U (residual underflow)
		pdu_response->ResidualCt = ResidualCt.value();
	}

	pdu_response_data.second = reply_data_plus_sense_header;
	if (pdu_response_data.second) {
		DOLOG(logging::ll_warning, "iscsi_pdu_scsi_response::set", ses->get_endpoint_name(), "CHECK CONDITION");

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

std::vector<blob_t> iscsi_pdu_scsi_response::get() const
{
	return get_helper(pdu_response, pdu_response_data.first, pdu_response_data.second);
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_scsi_data_in::iscsi_pdu_scsi_data_in(session *const ses): iscsi_pdu_bhs(ses)
{
	assert(sizeof(*pdu_data_in) == 48);
}

iscsi_pdu_scsi_data_in::~iscsi_pdu_scsi_data_in()
{
	delete [] pdu_data_in_data.first;
	delete reply_to_copy;
}

bool iscsi_pdu_scsi_data_in::set(const iscsi_pdu_scsi_cmd & reply_to, const std::pair<uint8_t *, size_t> scsi_reply_data, const bool has_sense)
{
	DOLOG(logging::ll_debug, "iscsi_pdu_scsi_data_in::set", ses->get_endpoint_name(), "%zu payload bytes, has_sense: %d", scsi_reply_data.second, has_sense);

	auto temp = reply_to.get_raw();
	reply_to_copy = new iscsi_pdu_scsi_cmd(ses);
	reply_to_copy->set(temp.data, temp.n);
	delete [] temp.data;

	pdu_data_in_data.second = scsi_reply_data.second;
	if (pdu_data_in_data.second) {
		pdu_data_in_data.first = duplicate_new(scsi_reply_data.first, pdu_data_in_data.second);
	}

	return true;
}

std::vector<blob_t> iscsi_pdu_scsi_data_in::get() const
{
	std::vector<blob_t> v_out;

	// resize to limit
	auto use_pdu_data_size = pdu_data_in_data.second;

	if (use_pdu_data_size > size_t(reply_to_copy->get_ExpDatLen()))
		DOLOG(logging::ll_warning, "iscsi_pdu_scsi_data_in", ses->get_endpoint_name(), "requested less (%zu) than wat is available (%zu)", size_t(reply_to_copy->get_ExpDatLen()), use_pdu_data_size);
	else if (use_pdu_data_size == 0)
		DOLOG(logging::ll_warning, "iscsi_pdu_scsi_data_in", ses->get_endpoint_name(), "trying to send DATA-IN without data");

	use_pdu_data_size = std::min(use_pdu_data_size, size_t(reply_to_copy->get_ExpDatLen()));

	auto   block_size = ses->get_block_size();
	size_t n_to_do    = (use_pdu_data_size + block_size - 1) / block_size;

	for(size_t i=0, count=0; i<use_pdu_data_size; i += block_size, count++) {  // 4kB blocks
		*pdu_data_in = { };
		set_bits(&pdu_data_in->b1, 0, 6, o_scsi_data_in);  // 0x25
		bool last_block = count == n_to_do - 1;
		if (last_block) {
			set_bits(&pdu_data_in->b2, 7, 1, true);  // F
			if (pdu_data_in_data.second < reply_to_copy->get_ExpDatLen()) {
				set_bits(&pdu_data_in->b2, 1, 1, true);  // U
				pdu_data_in->ResidualCt = my_HTONL(reply_to_copy->get_ExpDatLen() - pdu_data_in_data.second);
			}
			else if (pdu_data_in_data.second > reply_to_copy->get_ExpDatLen()) {
				set_bits(&pdu_data_in->b2, 2, 1, true);  // O
				pdu_data_in->ResidualCt = my_HTONL(pdu_data_in_data.second - reply_to_copy->get_ExpDatLen());
			}
			set_bits(&pdu_data_in->b2, 0, 1, true);  // S
		}
		size_t cur_len = std::min(use_pdu_data_size - i, size_t(block_size));
		DOLOG(logging::ll_debug, "iscsi_pdu_scsi_data_in::get", ses->get_endpoint_name(), "block %zu, last_block: %d, cur_len: %zu", count, last_block, cur_len);
		pdu_data_in->datalenH   = cur_len >> 16;
		pdu_data_in->datalenM   = cur_len >>  8;
		pdu_data_in->datalenL   = cur_len      ;
		memcpy(pdu_data_in->LUN, reply_to_copy->get_LUN(), sizeof pdu_data_in->LUN);
		pdu_data_in->Itasktag   = reply_to_copy->get_Itasktag();
		pdu_data_in->StatSN     = my_HTONL(reply_to_copy->get_ExpStatSN());
		pdu_data_in->ExpCmdSN   = my_HTONL(reply_to_copy->get_CmdSN() + 1);
		pdu_data_in->MaxCmdSN   = my_HTONL(reply_to_copy->get_CmdSN() + max_msg_depth);
		pdu_data_in->DataSN     = my_HTONL(ses->get_inc_datasn(reply_to_copy->get_Itasktag()));
		pdu_data_in->bufferoff  = my_HTONL(i);
		pdu_data_in->ResidualCt = my_HTONL(use_pdu_data_size - i);

		auto temp_out = get_helper(pdu_data_in, pdu_data_in_data.first + i, cur_len);
		v_out.push_back(temp_out.at(0));
	}

	DOLOG(logging::ll_debug, "iscsi_pdu_scsi_data_in::get", ses->get_endpoint_name(), "returning %zu PDUs", v_out.size());

	return v_out;
}

std::pair<blob_t, uint8_t *> iscsi_pdu_scsi_data_in::gen_data_in_pdu(session *const ses, const iscsi_pdu_scsi_cmd & reply_to, const size_t use_pdu_data_size, const size_t offset_in_data, const size_t data_is_n_bytes)
{
	uint64_t offset_after_block = offset_in_data + data_is_n_bytes;
	bool     last_block         = offset_after_block >= use_pdu_data_size;  // > in case iSCSI transfer length is less

	if (last_block)
		DOLOG(logging::ll_debug, "iscsi_pdu_scsi_data_in::gen_data_in_pdu", ses->get_endpoint_name(), "last block");
	else
		DOLOG(logging::ll_warning, "iscsi_pdu_scsi_data_in::gen_data_in_pdu", ses->get_endpoint_name(), "offset %zu + %zu != %zu", offset_in_data, data_is_n_bytes, use_pdu_data_size);

	__pdu_data_in__ pdu_data_in { };

	set_bits(&pdu_data_in.b1, 0, 6, o_scsi_data_in);  // 0x25
	if (last_block) {
		set_bits(&pdu_data_in.b2, 7, 1, true);  // F
		if (use_pdu_data_size < reply_to.get_ExpDatLen()) {
			set_bits(&pdu_data_in.b2, 1, 1, true);  // U
		}
		else if (offset_after_block > reply_to.get_ExpDatLen()) {
			set_bits(&pdu_data_in.b2, 2, 1, true);  // O
		}
		set_bits(&pdu_data_in.b2, 0, 1, true);  // S
	}
	pdu_data_in.datalenH   = data_is_n_bytes >> 16;
	pdu_data_in.datalenM   = data_is_n_bytes >>  8;
	pdu_data_in.datalenL   = data_is_n_bytes      ;
	memcpy(pdu_data_in.LUN, reply_to.get_LUN(), sizeof pdu_data_in.LUN);
	pdu_data_in.Itasktag   = reply_to.get_Itasktag();
	pdu_data_in.StatSN     = my_HTONL(reply_to.get_ExpStatSN());
	pdu_data_in.ExpCmdSN   = my_HTONL(reply_to.get_CmdSN() + 1);
	pdu_data_in.MaxCmdSN   = my_HTONL(reply_to.get_CmdSN() + max_msg_depth);
	pdu_data_in.DataSN     = my_HTONL(ses->get_inc_datasn(reply_to.get_Itasktag()));
	pdu_data_in.bufferoff  = my_HTONL(offset_in_data);

	if (reply_to.get_ExpDatLen() < offset_after_block)
		pdu_data_in.ResidualCt = my_HTONL(offset_after_block - reply_to.get_ExpDatLen());
	else
		pdu_data_in.ResidualCt = my_HTONL(reply_to.get_ExpDatLen() - (offset_in_data + data_is_n_bytes));

	size_t out_size = sizeof(pdu_data_in) + data_is_n_bytes;
	out_size = (out_size + 3) & ~3;

	if (ses->get_header_digest())
		out_size += sizeof(uint32_t);
	if (ses->get_data_digest())
		out_size += sizeof(uint32_t);

	uint8_t *out      = new (std::nothrow) uint8_t[out_size]();
	uint8_t *out_data = nullptr;
	if (out) {
		const size_t pdu_size = sizeof pdu_data_in;
		memcpy(out, &pdu_data_in, pdu_size);  // data is set by caller! (to reduce memcpy's)

		if (ses->get_header_digest()) {
			uint32_t crc32 = crc32_0x11EDC6F41(reinterpret_cast<const uint8_t *>(&pdu_data_in), pdu_size, { }).first;
			memcpy(&out[pdu_size], &crc32, sizeof crc32);
			out_data = &out[pdu_size + sizeof crc32];
		}
		else {
			out_data = &out[pdu_size];
		}
	}
	else {
		out_size = 0;
	}

	return { { out, out_size }, out_data };
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_scsi_data_out::iscsi_pdu_scsi_data_out(session *const ses): iscsi_pdu_bhs(ses)
{
	assert(sizeof(*pdu_data_out) == 48);
}

iscsi_pdu_scsi_data_out::~iscsi_pdu_scsi_data_out()
{
	delete [] pdu_data_out_data.first;
}

bool iscsi_pdu_scsi_data_out::set(const iscsi_pdu_scsi_cmd & reply_to, const std::pair<uint8_t *, size_t> scsi_reply_data)
{
	DOLOG(logging::ll_debug, "iscsi_pdu_scsi_data_out::set", ses->get_endpoint_name(), "with %zu payload bytes", scsi_reply_data.second);

	pdu_data_out_data.second = scsi_reply_data.second;
	if (pdu_data_out_data.second) {
		pdu_data_out_data.first = duplicate_new(scsi_reply_data.first, pdu_data_out_data.second);
	}

	return true;
}

std::vector<blob_t> iscsi_pdu_scsi_data_out::get() const
{
	return { };
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_nop_out::iscsi_pdu_nop_out(session *const ses): iscsi_pdu_bhs(ses)
{
	assert(sizeof(*nop_out) == 48);
}

iscsi_pdu_nop_out::~iscsi_pdu_nop_out()
{
}

std::optional<iscsi_response_set> iscsi_pdu_nop_out::get_response(scsi *const sd)
{
	DOLOG(logging::ll_debug, "iscsi_pdu_nop_out::get_response", ses->get_endpoint_name(), "invoked");

	iscsi_response_set response;
	auto reply_pdu = new iscsi_pdu_nop_in(ses);
	if (reply_pdu->set(*this) == false) {
		delete reply_pdu;
		return { };
	}
	response.responses.push_back(reply_pdu);

	return response;
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_nop_in::iscsi_pdu_nop_in(session *const ses): iscsi_pdu_bhs(ses)
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
	nop_in->StatSN     = my_HTONL(reply_to.get_ExpStatSN());
	nop_in->ExpCmdSN   = my_HTONL(reply_to.get_CmdSN() + 1);
	nop_in->MaxCmdSN   = my_HTONL(reply_to.get_CmdSN() + max_msg_depth);

	return true;
}

std::vector<blob_t> iscsi_pdu_nop_in::get() const
{
	return get_helper(nop_in, nullptr, 0);
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_scsi_r2t::iscsi_pdu_scsi_r2t(session *const ses): iscsi_pdu_bhs(ses)
{
	assert(sizeof(*pdu_scsi_r2t) == 48);
}

iscsi_pdu_scsi_r2t::~iscsi_pdu_scsi_r2t()
{
	delete [] pdu_scsi_r2t_data.first;
}

bool iscsi_pdu_scsi_r2t::set(const iscsi_pdu_scsi_cmd & reply_to, const uint32_t TTT, const uint32_t buffer_offset, const uint32_t data_length)
{
	*pdu_scsi_r2t = { };
	set_bits(&pdu_scsi_r2t->b1, 0, 6, o_r2t);
	set_bits(&pdu_scsi_r2t->b2, 7, 1, true);
	memcpy(pdu_scsi_r2t->LUN, reply_to.get_LUN(), sizeof pdu_scsi_r2t->LUN);
	pdu_scsi_r2t->Itasktag   = reply_to.get_Itasktag();
	pdu_scsi_r2t->TTT        = TTT;
	pdu_scsi_r2t->StatSN     = my_HTONL(reply_to.get_ExpStatSN());
	pdu_scsi_r2t->ExpCmdSN   = my_HTONL(reply_to.get_CmdSN() + 1);
	pdu_scsi_r2t->MaxCmdSN   = my_HTONL(reply_to.get_CmdSN() + max_msg_depth);
	pdu_scsi_r2t->bufferoff  = my_HTONL(buffer_offset);
	pdu_scsi_r2t->DDTF       = my_HTONL(data_length);

	return true;
}

std::vector<blob_t> iscsi_pdu_scsi_r2t::get() const
{
	return get_helper(pdu_scsi_r2t, nullptr, 0);
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_text_request::iscsi_pdu_text_request(session *const ses): iscsi_pdu_bhs(ses)
{
	assert(sizeof(*text_req) == 48);

	*text_req = { };
}

iscsi_pdu_text_request::~iscsi_pdu_text_request()
{
}

bool iscsi_pdu_text_request::set(const uint8_t *const in, const size_t n)
{
	if (iscsi_pdu_bhs::set(in, n) == false)
		return false;

	// TODO further validation

	return true;
}

std::vector<blob_t> iscsi_pdu_text_request::get() const
{
	return get_helper(text_req, nullptr, 0);
}

std::optional<iscsi_response_set> iscsi_pdu_text_request::get_response(scsi *const sd)
{
	iscsi_response_set response;
	auto reply_pdu = new iscsi_pdu_text_reply(ses);
	if (reply_pdu->set(*this, sd) == false) {
		delete reply_pdu;
		return { };
	}
	response.responses.push_back(reply_pdu);

	return response;
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_text_reply::iscsi_pdu_text_reply(session *const ses): iscsi_pdu_bhs(ses)
{
	assert(sizeof(*text_reply) == 48);
}

iscsi_pdu_text_reply::~iscsi_pdu_text_reply()
{
	delete [] text_reply_reply_data.first;
}

bool iscsi_pdu_text_reply::set(const iscsi_pdu_text_request & reply_to, scsi *const sd)
{
	const auto data = reply_to.get_data();
	if (data.has_value() == false)
		return false;
	auto kvs_in = data_to_text_array(data.value().first, data.value().second);
	bool send_targets = false;

	for(auto & kv: kvs_in) {
		auto parts = split(kv, "=");
		if (parts.size() < 2)
			return false;

		if (parts[0] == "SendTargets" && parts[1] == "All")
			send_targets = true;

		DOLOG(logging::ll_debug, "iscsi_pdu_text_reply::set", ses->get_endpoint_name(), "text request, responding to: %s", kv.c_str());
	}

	if (send_targets) {
		const std::vector<std::string> kvs {
			"TargetName=" + ses->get_target_name(),
			"TargetAddress=" + ses->get_local_address() + ",1",
		};
		auto temp = text_array_to_data(kvs);
		text_reply_reply_data.first  = temp.first;
		text_reply_reply_data.second = temp.second;
	}

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
	text_reply->StatSN     = my_HTONL(reply_to.get_ExpStatSN());
	text_reply->ExpCmdSN   = my_HTONL(reply_to.get_CmdSN() + 1);
	text_reply->MaxCmdSN   = my_HTONL(reply_to.get_CmdSN() + max_msg_depth);

	return true;
}

std::vector<blob_t> iscsi_pdu_text_reply::get() const
{
	return get_helper(text_reply, text_reply_reply_data.first, text_reply_reply_data.second);
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_logout_request::iscsi_pdu_logout_request(session *const ses): iscsi_pdu_bhs(ses)
{
	assert(sizeof(*logout_req) == 48);

	*logout_req = { };
}

iscsi_pdu_logout_request::~iscsi_pdu_logout_request()
{
}

bool iscsi_pdu_logout_request::set(const uint8_t *const in, const size_t n)
{
	if (iscsi_pdu_bhs::set(in, n) == false)
		return false;

	// TODO further validation

	return true;
}

std::vector<blob_t> iscsi_pdu_logout_request::get() const
{
	return get_helper(logout_req, nullptr, 0);
}

std::optional<iscsi_response_set> iscsi_pdu_logout_request::get_response(scsi *const sd)
{
	iscsi_response_set response;
	auto reply_pdu = new iscsi_pdu_logout_reply(ses);
	if (reply_pdu->set(*this) == false) {
		delete reply_pdu;
		return { };
	}
	response.responses.push_back(reply_pdu);

	return response;
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_logout_reply::iscsi_pdu_logout_reply(session *const ses): iscsi_pdu_bhs(ses)
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
	logout_reply->StatSN   = my_HTONL(reply_to.get_ExpStatSN());
	logout_reply->ExpCmdSN = my_HTONL(reply_to.get_CmdSN());
	logout_reply->MaxCmdSN = my_HTONL(reply_to.get_CmdSN());

	return true;
}

std::vector<blob_t> iscsi_pdu_logout_reply::get() const
{
	return get_helper(logout_reply, nullptr, 0);
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_taskman_request::iscsi_pdu_taskman_request(session *const ses): iscsi_pdu_bhs(ses)
{
	assert(sizeof(*taskman_req) == 48);

	*taskman_req = { };
}

iscsi_pdu_taskman_request::~iscsi_pdu_taskman_request()
{
}

bool iscsi_pdu_taskman_request::set(const uint8_t *const in, const size_t n)
{
	if (iscsi_pdu_bhs::set(in, n) == false)
		return false;

	// TODO further validation

	return true;
}

std::vector<blob_t> iscsi_pdu_taskman_request::get() const
{
	return get_helper(taskman_req, nullptr, 0);
}

std::optional<iscsi_response_set> iscsi_pdu_taskman_request::get_response(scsi *const sd)
{
	iscsi_response_set response;
	auto reply_pdu = new iscsi_pdu_taskman_reply(ses);
	if (reply_pdu->set(*this) == false) {
		delete reply_pdu;
		return { };
	}
	response.responses.push_back(reply_pdu);

	return response;
}

/*--------------------------------------------------------------------------*/

iscsi_pdu_taskman_reply::iscsi_pdu_taskman_reply(session *const ses): iscsi_pdu_bhs(ses)
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
	taskman_reply->StatSN   = my_HTONL(reply_to.get_ExpStatSN());
	taskman_reply->ExpCmdSN = my_HTONL(reply_to.get_CmdSN() + 1);
	taskman_reply->MaxCmdSN = my_HTONL(reply_to.get_CmdSN() + max_msg_depth);

	return true;
}

std::vector<blob_t> iscsi_pdu_taskman_reply::get() const
{
	return get_helper(taskman_reply, nullptr, 0);
}

std::optional<blob_t> generate_reject_pdu(const iscsi_pdu_bhs & about)
{
	struct __reject__ {
		uint8_t  b1;
		// uint8_t  opcode    :  6;

		uint8_t  b2;
		// bool     reserved1 :  7;
		// bool     F         :  1;  // bit 7

		uint8_t  reason;
		uint8_t  reserved1;

		uint8_t  ahslen    :  8;  // total ahs length (units of four byte words including padding)
		uint32_t datalenH  :  8;  // data segment length (bytes, excluding padding) 23...16
		uint32_t datalenM  :  8;  // data segment length (bytes, excluding padding) 15...8
		uint32_t datalenL  :  8;  // data segment length (bytes, excluding padding) 7...0
		uint8_t  reserved2[8];
		uint32_t ffffffff  : 32;  // 0xffffffff
		uint32_t reserved3 : 32;
		uint32_t StatSN    : 32;
		uint32_t ExpCmdSN  : 32;
		uint32_t MaxCmdSN  : 32;
		uint32_t DataSN    : 32;
		uint32_t reserved4 : 32;
		uint32_t reserved5 : 32;

		uint8_t  problem_pdu[48];
	};

	assert(sizeof(__reject__) == 48 * 2);

	__reject__ *reject = new __reject__();
	auto raw = about.get();
	if (raw.empty()) {
		delete reject;
		DOLOG(logging::ll_error, "generate_reject_pdu", "-", "can't get data from original PDU");
		return { };
	}
	memcpy(reject->problem_pdu, raw[0].data, 48);
	delete [] raw[0].data;
	reject->datalenL = 48;

	set_bits(&reject->b1, 0, 6, iscsi_pdu_bhs::iscsi_bhs_opcode::o_reject);  // 0x3f
	set_bits(&reject->b2, 7, 1, true);

	reject->reason = 0x09;  // (invalid PDU field)

	size_t s = sizeof *reject;
	blob_t out { duplicate_new(reinterpret_cast<uint8_t *>(reject), s), s };
	delete reject;
	return out;
}
