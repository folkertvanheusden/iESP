#include "iscsi-pdu.h"
#include "log.h"
#include "random.h"
#include "session.h"


session::session(com_client *const connected_to, const std::string & target_name, const bool allow_digest):
	connected_to(connected_to),
	target_name(target_name),
	allow_digest(allow_digest)
{
}

session::~session()
{
	for(auto & it: r2t_sessions) {
		delete [] it.second->PDU_initiator.data;
		delete it.second;
	}
}

uint32_t session::get_inc_datasn(const uint32_t data_sn_itt)
{
	if (data_sn_itt == this->data_sn_itt)
		return data_sn++;

	this->data_sn_itt = data_sn_itt;
	this->data_sn     = 0;

	return data_sn++;
}

bool session::init_r2t_session(const r2t_session & rs, const bool fua, iscsi_pdu_scsi_cmd *const pdu, const uint32_t transfer_tag)
{
	if (r2t_sessions.size() > MAX_N_CONCURRENT_R2T) {
		DOLOG(logging::ll_warning, "session::init_r2t_session", get_endpoint_name(), "maximum number of parallel R2T sessions reached");
		return false;
	}

	auto it = r2t_sessions.find(transfer_tag);
	if (it != r2t_sessions.end()) {
		DOLOG(logging::ll_error, "session::init_r2t_session", get_endpoint_name(), "R2T session already registered");
		return false;
	}

	r2t_session   *copy = new r2t_session;
	*copy               = rs;
	copy->fua           = fua;
	copy->PDU_initiator = pdu->get_raw();

	DOLOG(logging::ll_debug, "session::init_r2t_session", get_endpoint_name(), "register transfer tag %08x", transfer_tag);
	r2t_sessions.insert({ transfer_tag, copy });

	return true;
}

r2t_session *session::get_r2t_sesion(const uint32_t ttt)
{
	DOLOG(logging::ll_debug, "session::get_r2t_session", get_endpoint_name(), "get TTT %08x", ttt);
	auto it = r2t_sessions.find(ttt);
	if (it == r2t_sessions.end())
		return nullptr;

	return it->second;
}

void session::remove_r2t_session(const uint32_t ttt)
{
	auto it = r2t_sessions.find(ttt);

	if (it == r2t_sessions.end())
		DOLOG(logging::ll_error, "session::remove_r2t_session", get_endpoint_name(), "unexpected TTT (%x)", ttt);
	else {
		DOLOG(logging::ll_debug, "session::remove_r2t_session", get_endpoint_name(), "removing TTT %x", ttt);
		delete [] it->second->PDU_initiator.data;
		delete it->second;
		r2t_sessions.erase(it);
	}
}

uint8_t session::get_chap_I()
{
	if (chap_I.has_value() == false) {
		uint32_t temp = 0;
		my_getrandom(&temp);  // what to do if this fails?
		chap_I = temp;
	}
	return chap_I.value();
}

std::string session::get_chap_C()
{
	if (chap_C.has_value() == false) {
		uint8_t random[16] {};
		my_getrandom(random, sizeof random);  // TODO can fail
		chap_C = "0x" + to_hex(random, sizeof random);
	}
	return chap_C.value();
}
