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

void session::init_r2t_session(const uint32_t ttt, const r2t_session & rs)
{
	r2t_ttt  = ttt;
	this->rs = rs;
}

r2t_session *session::get_r2t_sesion(const uint32_t ttt)
{
	if (ttt != r2t_ttt)
		return nullptr;

	return &rs;
}
