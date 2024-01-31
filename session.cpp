#include "log.h"
#include "random.h"
#include "session.h"


session::session()
{
}

session::~session()
{
}

uint32_t session::get_inc_datasn(const uint32_t data_sn_itt)
{
	if (data_sn_itt == this->data_sn_itt)
		return data_sn++;

	this->data_sn_itt = data_sn_itt;
	this->data_sn     = 0;

	return data_sn++;
}

uint32_t session::init_r2t_session(const r2t_session & rs)
{
	r2t_session *copy = new r2t_session;
	*copy = rs;

	uint32_t temp_TTT = 0;
	do {
		my_getrandom(&temp_TTT, sizeof temp_TTT);
	}
	while(r2t_sessions.find(temp_TTT) != r2t_sessions.end());

	r2t_sessions.insert({ temp_TTT, copy });

	return temp_TTT;
}

r2t_session *session::get_r2t_sesion(const uint32_t ttt)
{
	auto it = r2t_sessions.find(ttt);
	if (it == r2t_sessions.end())
		return nullptr;

	return it->second;
}

void session::remove_r2t_session(const uint32_t ttt)
{
	auto it = r2t_sessions.find(ttt);

	if (it == r2t_sessions.end())
		DOLOG("session::remove_r2t_session: unexpected TTT (%x)\n", ttt);
	else {
		delete it->second;
		r2t_sessions.erase(it);
	}
}
