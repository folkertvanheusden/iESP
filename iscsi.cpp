#include <cassert>
#include <cstring>

#include "iscsi.h"


iscsi_pdu_bhs::iscsi_pdu_bhs()
{
	assert(sizeof(bhs) == 48);

	bhs = { 0 };
}

iscsi_pdu_bhs::~iscsi_pdu_bhs()
{
}

ssize_t iscsi_pdu_bhs::set(const uint8_t *const in, const size_t n)
{
	assert(n == 48);

	memcpy(&bhs, in, sizeof bhs);

	return sizeof bhs;
}

std::pair<const uint8_t *, std::size_t> iscsi_pdu_bhs::get()
{
	void *out = new uint8_t[sizeof bhs];
	memcpy(out, &bhs, sizeof bhs);

	return { reinterpret_cast<const uint8_t *>(out), sizeof bhs };
}
