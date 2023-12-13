#include <cassert>
#include <cstring>
#include <arpa/inet.h>

#include "iscsi-pdu.h"


// BHS

iscsi_pdu_bhs::iscsi_pdu_bhs()
{
	assert(sizeof(bhs) == 48);

	bhs = { };
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

// AHS

iscsi_pdu_ahs::iscsi_pdu_ahs()
{
	assert(sizeof(__ahs_header__) == 3);
}

iscsi_pdu_ahs::~iscsi_pdu_ahs()
{
	delete ahs;
}

ssize_t iscsi_pdu_ahs::set(const uint8_t *const in, const size_t n)
{
	assert(n >= 3);

	size_t expected_size = ntohs(reinterpret_cast<const __ahs_header__ *>(in)->length);
	assert(expected_size + 3 == n);

	delete ahs;
	ahs = reinterpret_cast<__ahs_header__ *>(new uint8_t[expected_size + 3]);
	memcpy(ahs, in, n);

	return expected_size + 3;
}

std::pair<const uint8_t *, std::size_t> iscsi_pdu_ahs::get()
{
	uint16_t expected_size = ntohs(reinterpret_cast<const __ahs_header__ *>(ahs)->length);
	uint32_t out_size      = sizeof(__ahs_header__) + expected_size;
	void *out = new uint8_t[out_size];
	memcpy(out, ahs, out_size);

	return { reinterpret_cast<const uint8_t *>(out), out_size };
}
