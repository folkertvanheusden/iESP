#include "iscsi-pdu.h"
#include "log.h"
#include "random.h"
#include "session.h"


session::session(com_client *const connected_to, const std::string & target_name):
	connected_to(connected_to),
	target_name(target_name)
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

uint32_t session::init_r2t_session(const r2t_session & rs, iscsi_pdu_scsi_cmd *const pdu)
{
	uint32_t ITT = pdu->get_Itasktag();

	auto it = r2t_sessions.find(ITT);
	if (it != r2t_sessions.end())
		return ITT;

	r2t_session *copy = new r2t_session;
	*copy               = rs;
	copy->PDU_initiator = pdu->get_raw();

	DOLOG(logging::ll_debug, "session::init_r2t_session", get_endpoint_name(), "register ITT %08x", ITT);
	r2t_sessions.insert({ ITT, copy });

	return ITT;
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
