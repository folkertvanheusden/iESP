// (C) 2022-2024 by folkert van heusden <mail@vanheusden.com>, released under Apache License v2.0
#include <cstdint>
#include <thread>
#include <unistd.h>
#if defined(ESP32)
#include <Arduino.h>
#elif defined(TEENSY4_1)
#include <NativeEthernet.h>
#include <NativeEthernetUdp.h>
#else
#include <arpa/inet.h>
#include <poll.h>
#endif
#if !defined(TEENSY4_1)
#include <sys/socket.h>
#include <sys/types.h>
#endif

#include "../log.h"
#include "../utils.h"
#include "snmp.h"
#include "snmp_elem.h"


snmp::snmp(snmp_data *const sd, std::atomic_bool *const stop): sd(sd), stop(stop)
{
#if !defined(ARDUINO) || defined(ESP32)
	fd = socket(AF_INET, SOCK_DGRAM, 0);

	sockaddr_in servaddr { };
	servaddr.sin_family      = AF_INET; // IPv4
	servaddr.sin_addr.s_addr = INADDR_ANY;
	servaddr.sin_port        = htons(161);

	if (bind(fd, reinterpret_cast<const struct sockaddr *>(&servaddr), sizeof servaddr) == -1)
		DOLOG("Failed to bind to SNMP UDP port\n");
#else
	handle = new EthernetUDP();
	handle->begin(161);
#endif
	buffer = new uint8_t[SNMP_RECV_BUFFER_SIZE]();

#if !defined(TEENSY4_1)
	th = new std::thread(&snmp::thread, this);
#endif
}

snmp::~snmp()
{
#if !defined(ARDUINO) || defined(ESP32)
	close(fd);
#endif
#if defined(TEENSY4_1)
	delete handle;
#endif

	th->join();
	delete th;

	delete [] buffer;
}

uint64_t snmp::get_INTEGER(const uint8_t *p, const size_t length)
{
	uint64_t v = 0;

	if (length > 8)
		DOLOG("SNMP: INTEGER truncated (%zu bytes)\n", length);

	for(size_t i=0; i<length; i++) {
		v <<= 8;
		v |= *p++;
	}

	return v;
}

bool snmp::get_type_length(const uint8_t *p, const size_t len, uint8_t *const type, uint8_t *const length)
{
	if (len < 2) {
		DOLOG("snmp::get_type_length: length < 2\n");
		return false;
	}

	*type = *p++;

	*length = *p++;

	return true;
}

bool snmp::get_OID(const uint8_t *p, const size_t length, std::string *const oid_out)
{
	oid_out->clear();

	uint32_t v = 0;

	for(size_t i=0; i<length; i++) {
		if (p[i] < 128) {
			v <<= 7;
			v |= p[i];

			if (i == 0 && v == 43)
				*oid_out += "1.3";
			else
				*oid_out += myformat(".%d", v);

			v = 0;
		}
		else {
			v <<= 7;
			v |= p[i] & 127;
		}
	}

	if (v) {
		DOLOG("SNMP: object identifier did not properly terminate\n");
		return false;
	}

	return true;
}

bool snmp::process_PDU(const uint8_t *p, const size_t len, oid_req_t *const oids_req, const bool is_getnext)
{
	uint8_t pdu_type = 0, pdu_length = 0;

	// ID
	if (!get_type_length(p, len, &pdu_type, &pdu_length))
		return false;

	if (pdu_type != 0x02) { // expecting an integer here)
		DOLOG("SNMP::process_PDU: ID-type is not integer\n");
		return false;
	}

	p += 2;

	oids_req->req_id = get_INTEGER(p, pdu_length);
	p += pdu_length;

	// error
	if (!get_type_length(p, len, &pdu_type, &pdu_length))
		return false;

	if (pdu_type != 0x02) { // expecting an integer here)
		DOLOG("SNMP::process_PDU: error-type is not integer\n");
		return false;
	}

	p += 2;

	uint64_t error = get_INTEGER(p, pdu_length);
	(void)error;
	p += pdu_length;

	// error index
	if (!get_type_length(p, len, &pdu_type, &pdu_length))
		return false;

	if (pdu_type != 0x02) { // expecting an integer here)
		DOLOG("SNMP::process_PDU: error-index is not integer\n");
		return false;
	}

	p += 2;

	uint64_t error_index = get_INTEGER(p, pdu_length);
	(void)error_index;
	p += pdu_length;

	// varbind list sequence
	uint8_t type_vb_list = *p++;
	if (type_vb_list != 0x30) {
		DOLOG("SNMP::process_PDU: expecting varbind list sequence, got %02x\n", type_vb_list);
		return false;
	}
	uint8_t len_vb_list = *p++;

	const uint8_t *pnt = p;

	while(pnt < &p[len_vb_list]) {
		uint8_t seq_type = *pnt++;
		uint8_t seq_length = *pnt++;

		if (&pnt[seq_length] > &p[len_vb_list]) {
			DOLOG("SNMP: length field out of bounds (PDU)\n");
			return false;
		}

		if (seq_type == 0x30) {  // sequence
			process_BER(pnt, seq_length, oids_req, is_getnext, 0);
			pnt += seq_length;
		}
		else {
			DOLOG("SNMP: unexpected/invalid type %02x\n", seq_type);
			return false;
		}
	}

	return true;
}

bool snmp::process_BER(const uint8_t *p, const size_t len, oid_req_t *const oids_req, const bool is_getnext, const int is_top)
{
	if (len < 2) {
		DOLOG("SNMP: BER too small\n");
		return false;
	}

	const uint8_t *pnt   = p;
	bool first_integer   = true;
	bool first_octet_str = true;

	while(pnt < &p[len]) {
		uint8_t type   = *pnt++;
		uint8_t length = *pnt++;

		if (&pnt[length] > &p[len]) {
			DOLOG("SNMP: length field out of bounds (BER)\n");
			return false;
		}

		if (type == 0x02) {  // integer
			if (is_top && first_integer)
				oids_req->version = get_INTEGER(pnt, length);

			first_integer = false;

			pnt += length;
		}
		else if (type == 0x04) {  // octet string
			if (is_top && first_octet_str)
				oids_req->community = std::string(reinterpret_cast<const char *>(pnt), length);

			first_octet_str = false;

			pnt += length;
		}
		else if (type == 0x05) {  // null
			// ignore for now
			pnt += length;
		}
		else if (type == 0x06) {  // object identifier
			std::string oid_out;

			if (!get_OID(pnt, length, &oid_out))
				return false;

			if (is_getnext) {
				std::string oid_next = sd->find_next_oid(oid_out);

				if (oid_next.empty()) {
					oids_req->err = 2;
					oids_req->err_idx = 1;
				}
				else {
					oids_req->oids.push_back(oid_next);
				}
			}
			else {
				oids_req->oids.push_back(oid_out);
			}

			pnt += length;
		}
		else if (type == 0x30) {  // sequence
			if (!process_BER(pnt, length, oids_req, is_getnext, is_top - 1))
				return false;

			pnt += length;
		}
		else if (type == 0xa0) {  // GetRequest PDU
			if (!process_PDU(pnt, length, oids_req, is_getnext))
				return false;
			pnt += length;
		}
		else if (type == 0xa1) {  // GetNextRequest PDU
			if (!process_PDU(pnt, length, oids_req, true))
				return false;
			pnt += length;
		}
		else if (type == 0xa3) {  // SetRequest PDU
			if (!process_PDU(pnt, length, oids_req, is_getnext))
				return false;
			pnt += length;
		}
		else {
			DOLOG("SNMP: invalid type %02x\n", type);
			return false;
		}
	}

	return true;
}

void snmp::gen_reply(oid_req_t & oids_req, uint8_t **const packet_out, size_t *const output_size)
{
	snmp_sequence *se = new snmp_sequence();

	se->add(new snmp_integer(snmp_integer::si_integer, oids_req.version));  // version

	std::string community = oids_req.community;
	if (community.empty())
		community = "public";

	se->add(new snmp_octet_string((const uint8_t *)community.c_str(), community.size()));  // community string

	// request pdu
	snmp_pdu *GetResponsePDU = new snmp_pdu(0xa2);
	se->add(GetResponsePDU);

	GetResponsePDU->add(new snmp_integer(snmp_integer::si_integer, oids_req.req_id));  // ID

	GetResponsePDU->add(new snmp_integer(snmp_integer::si_integer, oids_req.err));  // error

	GetResponsePDU->add(new snmp_integer(snmp_integer::si_integer, oids_req.err_idx));  // error index

	snmp_sequence *varbind_list = new snmp_sequence();
	GetResponsePDU->add(varbind_list);

	for(auto & e : oids_req.oids) {
		snmp_sequence *varbind = new snmp_sequence();
		varbind_list->add(varbind);

		varbind->add(new snmp_oid(e));

		DOLOG("SNMP requested: %s\n", e.c_str());

		std::optional<snmp_elem *> rc = sd->find_by_oid(e);

		std::size_t dot       = e.rfind('.');
		std::string ends_with = dot != std::string::npos ? e.substr(dot) : "";

		if (!rc.has_value() && ends_with == ".0")
			rc = sd->find_by_oid(e.substr(0, dot));

		if (rc.has_value()) {
			auto current_element = rc.value();

			if (current_element)
				varbind->add(current_element);
			else
				varbind->add(new snmp_null());
		}
		else {
			DOLOG("SNMP: requested %s not found, returning null\n", e.c_str());

			// FIXME snmp_null?
			varbind->add(new snmp_null());
		}
	}

	auto rc = se->get_payload();
	*packet_out = rc.first;
	*output_size = rc.second;

	delete se;
}

#if defined(TEENSY4_1)
void snmp::poll()
{
	int rc = handle->parsePacket();
	if (rc == 0)
		return;

	if (rc > 0) {
		Serial.println(F("DAAR"));
		oid_req_t or_;

		handle->read(buffer, rc);

		if (!process_BER(buffer, rc, &or_, false, 2))
			return;

		uint8_t *packet_out  = nullptr;
		size_t   output_size = 0;
		gen_reply(or_, &packet_out, &output_size);
		if (output_size) {
	Serial.println(F("send"));
			handle->beginPacket(handle->remoteIP(), handle->remotePort());
			handle->write(packet_out, output_size);
			handle->endPacket();
		}
		free(packet_out);
	}
}
#else
void snmp::thread()
{
#if !defined(ARDUINO) || defined(ESP32)
	pollfd fds[] { { fd, POLLIN, 0 } };
#endif

	while(!*stop) {
#if !defined(ARDUINO) || defined(ESP32)
		sockaddr_in clientaddr   {   };
		socklen_t   len          { sizeof clientaddr };

		if (poll(fds, 1, 100) == 0)
			continue;

		int rc = recvfrom(fd, buffer, SNMP_RECV_BUFFER_SIZE, 0, reinterpret_cast<sockaddr *>(&clientaddr), &len);
		if (rc == -1)
			break;
#else
		int rc = handle->parsePacket();
		if (rc == 0) {
			delay(1);
			continue;
		}

		handle->read(buffer, rc);
#endif

		if (rc > 0) {
			oid_req_t or_;

			if (!process_BER(buffer, rc, &or_, false, 2))
				return;

			uint8_t *packet_out  = nullptr;
			size_t   output_size = 0;
			gen_reply(or_, &packet_out, &output_size);
			if (output_size) {
#if !defined(ARDUINO) || defined(ESP32)
				if (sendto(fd, packet_out, output_size, 0, reinterpret_cast<sockaddr *>(&clientaddr), len) == -1)
					errlog("Failed to transmit SNMP reply packet\n");
#else
				handle->beginPacket(handle->remoteIP(), handle->remotePort());
				handle->write(packet_out, output_size);
				handle->endPacket();
#endif
			}
			free(packet_out);
		}
	}
}
#endif
