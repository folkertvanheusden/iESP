#pragma once
#include <cstdint>
#include <map>

#include "iscsi.h"


class session
{
private:
	uint32_t data_sn_itt { 0 };  // itt == initiator transfer tag
	uint32_t data_sn     { 0 };

	std::map<uint32_t, r2t_session *> r2t_sessions; // r2t sessions

public:
	session();
	virtual ~session();

	uint32_t get_inc_datasn(const uint32_t itt);

	uint32_t init_r2t_session(const r2t_session & rs);
	r2t_session *get_r2t_sesion(const uint32_t ttt);
	void remove_r2t_session(const uint32_t ttt);
};
