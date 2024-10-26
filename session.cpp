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

void session::init_r2t_session(const r2t_session & rs, const bool fua, iscsi_pdu_scsi_cmd *const pdu, const uint32_t transfer_tag)
{
	auto it = r2t_sessions.find(transfer_tag);
	if (it != r2t_sessions.end())
		return;

	r2t_session   *copy = new r2t_session;
	*copy               = rs;
	copy->fua           = fua;
	copy->PDU_initiator = pdu->get_raw();

	DOLOG(logging::ll_debug, "session::init_r2t_session", get_endpoint_name(), "register transfer tag %08x", transfer_tag);
	r2t_sessions.insert({ transfer_tag, copy });
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
