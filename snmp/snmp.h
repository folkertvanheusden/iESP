// (C) 2022-2025 by folkert van heusden <mail@vanheusden.com>, released under Apache License v2.0
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>
#ifdef UNITTEST
#include <gtest/gtest.h>
#endif
#if defined(TEENSY4_1)
#include <QNEthernet.h>
namespace qn = qindesign::network;
#endif

#include "snmp_data.h"


#define SNMP_RECV_BUFFER_SIZE 65507  // 6556 - headers

typedef struct _oid_req_t_ {
	std::vector<std::string> oids;
	uint64_t req_id  { 0 };
	int      err     { 0 };
	int      err_idx { 0 };

	int version      { 0 };
	std::string community;

	_oid_req_t_() {
	}
} oid_req_t;

class block;

class snmp
{
private:
	snmp_data        *const sd        { nullptr };
#if !defined(ARDUINO) || defined(ESP32)
        int               fd              { -1      };
#elif defined(TEENSY4_1)
        qn::EthernetUDP  *handle          { nullptr };
#endif
	uint8_t          *buffer          { nullptr };  // for receiving requests
	std::thread      *th              { nullptr };
	std::atomic_bool *const stop_flag { nullptr };
	const bool        verbose         { false   };
	const int         port            { -1      };

#ifdef UNITTEST
	FRIEND_TEST(Snmp, test);
#endif

	bool     process_BER     (block *const b, oid_req_t *const oids_req, const bool is_getnext, const int is_top);
	uint64_t get_INTEGER     (block *const b);
	bool     get_OID         (block *const b, std::string *const oid_out);
	void     get_type_length (block *const b, uint8_t *const type, uint8_t *const len);
	bool     process_PDU     (block *const b, oid_req_t *const oids_req, const bool is_getnext);

	void     add_oid         (uint8_t **const packet_out, size_t *const output_size, const std::string & oid);
	void     add_octet_string(uint8_t **const packet_out, size_t *const output_size, const char *const str);
	void     gen_reply       (oid_req_t & oids_req, uint8_t **const packet_out, size_t *const output_size);

	void     thread          ();

public:
	snmp(snmp_data *const sd, std::atomic_bool *const stop, const bool verbose, const int port);
	snmp(const snmp &) = delete;
	virtual ~snmp();

	bool     begin();
	void     stop();

#if defined(TEENSY4_1)
        void     poll       ();
#endif
};
