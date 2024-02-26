// (C) 2022-2024 by folkert van heusden <mail@vanheusden.com>, released under MIT license
#pragma once
#include <atomic>
#include <stdint.h>
#include <string>
#include <thread>
#include <vector>

#include "snmp_data.h"


typedef struct _oid_req_t_ {
	std::vector<std::string> oids;
	uint64_t req_id { 0 };
	int err { 0 }, err_idx { 0 };

	int version { 0 };
	std::string community;

	_oid_req_t_() {
	}
} oid_req_t;

class snmp
{
private:
	snmp_data *const sd { nullptr };
	int              fd { -1      };
	std::thread     *th { nullptr };
	std::atomic_bool *const stop { nullptr };

	bool     process_BER(const uint8_t *p, const size_t len, oid_req_t *const oids_req, const bool is_getnext, const int is_top);
	uint64_t get_INTEGER(const uint8_t *p, const size_t len);
	bool     get_OID    (const uint8_t *p, const size_t length, std::string *const oid_out);
	bool     get_type_length(const uint8_t *p, const size_t len, uint8_t *const type, uint8_t *const length);
	bool     process_PDU(const uint8_t*, const size_t, oid_req_t *const oids_req, const bool is_getnext);

	void     add_oid    (uint8_t **const packet_out, size_t *const output_size, const std::string & oid);
	void     add_octet_string(uint8_t **const packet_out, size_t *const output_size, const char *const str);
	void     gen_reply  (oid_req_t & oids_req, uint8_t **const packet_out, size_t *const output_size);

	void     thread     ();

public:
	snmp(snmp_data *const sd, std::atomic_bool *const stop);
	snmp(const snmp &) = delete;
	virtual ~snmp();
};
