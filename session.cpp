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

uint32_t session::init_r2t_session(const r2t_session & rs, const bool fua, iscsi_pdu_scsi_cmd *const pdu)
{
	uint32_t ITT = pdu->get_Itasktag();

	auto it = r2t_sessions.find(ITT);
	if (it != r2t_sessions.end())
		return ITT;

	r2t_session *copy = new r2t_session;
	*copy               = rs;
	copy->fua           = fua;
	copy->PDU_initiator = pdu->get_raw();

	DOLOG("session::init_r2t_session: register ITT %08x\n", ITT);
	r2t_sessions.insert({ ITT, copy });

	return ITT;
}

r2t_session *session::get_r2t_sesion(const uint32_t ttt)
{
	DOLOG("session::get_r2t_session: get TTT %08x\n", ttt);
	auto it = r2t_sessions.find(ttt);
	if (it == r2t_sessions.end())
		return nullptr;

	return it->second;
}

void session::remove_r2t_session(const uint32_t ttt)
{
	auto it = r2t_sessions.find(ttt);

	if (it == r2t_sessions.end())
		errlog("session::remove_r2t_session: unexpected TTT (%x)", ttt);
	else {
		DOLOG("session::remove_r2t_session: removing TTT %x\n", ttt);
		delete [] it->second->PDU_initiator.data;
		delete it->second;
		r2t_sessions.erase(it);
	}
}
