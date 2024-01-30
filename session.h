#pragma once
#include <cstdint>

#include "iscsi.h"


class session
{
private:
	uint32_t data_sn_itt { 0 };  // itt == initiator transfer tag
	uint32_t data_sn     { 0 };

	uint32_t    r2t_ttt { 0 };  // target transfer tag
	r2t_session rs      { 0 };

public:
	session();
	virtual ~session();

	uint32_t get_inc_datasn(const uint32_t itt);

	void init_r2t_session(const uint32_t ttt, const r2t_session & rs);
	r2t_session *get_r2t_sesion(const uint32_t ttt);
};
