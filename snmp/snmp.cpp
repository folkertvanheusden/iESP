// (C) 2022-2025 by folkert van heusden <mail@vanheusden.com>, released under Apache License v2.0
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#if defined(ESP32)
#include <Arduino.h>
#elif defined(TEENSY4_1)
#include <QNEthernet.h>
namespace qn = qindesign::network;
#elif defined(__MINGW32__)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <poll.h>
#endif
#if !defined(TEENSY4_1) && !defined(__MINGW32__)
#include <sys/socket.h>
#include <sys/types.h>
#endif
#if defined(__FreeBSD__)
#include <netinet/in.h>
#endif

#include "block.h"
#include "snmp.h"
#include "snmp_elem.h"
#include "../utils.h"


snmp::snmp(snmp_data *const sd, std::atomic_bool *const stop_flag, const bool verbose, const int port): sd(sd), stop_flag(stop_flag), verbose(verbose), port(port)
{
}

snmp::~snmp()
{
	stop();
}

bool snmp::begin()
{
#if !defined(ARDUINO) || defined(ESP32)
	if (fd != -1)
		return false;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd == -1)
		return false;

	sockaddr_in servaddr { };
	servaddr.sin_family      = AF_INET; // IPv4
	servaddr.sin_addr.s_addr = INADDR_ANY;
	servaddr.sin_port        = htons(port);

	if (bind(fd, reinterpret_cast<const struct sockaddr *>(&servaddr), sizeof servaddr) == -1)
		return false;
#else
        handle = new qn::EthernetUDP();
        handle->begin(port);
#endif

	buffer = new uint8_t[SNMP_RECV_BUFFER_SIZE]();

#if !defined(TEENSY4_1)
	*stop_flag = false;
	th = new std::thread(&snmp::thread, this);
#endif

	return true;
}

void snmp::stop()
{
#if !defined(TEENSY4_1)
	if (fd != -1) {
		close(fd);
		fd = -1;
	}

	if (th) {
		*stop_flag = true;
		th->join();
		delete th;
		th = nullptr;
	}
#endif

	delete [] buffer;
	buffer = nullptr;
}

uint64_t snmp::get_INTEGER(block *const b)
{
	uint64_t v = 0;
	while(b->is_empty() == false) {
		v <<= 8;
		v |= b->get_byte();
	}

	return v;
}

void snmp::get_type_length(block *const b, uint8_t *const type, uint8_t *const length)
{
	*type   = b->get_byte();
	*length = b->get_byte();
}

bool snmp::get_OID(block *const b, std::string *const oid_out)
{
	oid_out->clear();

	bool     first = true;
	uint32_t v     = 0;
	while(b->is_empty() == false) {
		uint8_t cur_byte = b->get_byte();
		if (cur_byte < 128) {
			v <<= 7;
			v |= cur_byte;

			if (first == true && v == 43)
				*oid_out += "1.3";
			else
				*oid_out += myformat(".%d", v);

			v = 0;
		}
		else {
			v <<= 7;
			v |= cur_byte & 127;
		}

		first = false;
	}

	if (v) {
		if (verbose)
			printf("SNMP: object identifier did not properly terminate\n");
		return false;
	}

	return true;
}

bool snmp::process_PDU(block *const b, oid_req_t *const oids_req, const bool is_getnext)
{
	uint8_t pdu_type = 0, pdu_length = 0;

	// ID
	get_type_length(b, &pdu_type, &pdu_length);
	if (pdu_type != 0x02) { // expecting an integer here)
		if (verbose)
			printf("SNMP::process_PDU: ID-type is not integer\n");
		return false;
	}
	block id_bytes(b->get_bytes(pdu_length));
	oids_req->req_id = get_INTEGER(&id_bytes);

	// error
	get_type_length(b, &pdu_type, &pdu_length);
	if (pdu_type != 0x02) { // expecting an integer here)
		if (verbose)
			printf("SNMP::process_PDU: error-type is not integer\n");
		return false;
	}
	b->skip_bytes(pdu_length);

	// error index
	get_type_length(b, &pdu_type, &pdu_length);
	if (pdu_type != 0x02) { // expecting an integer here)
		if (verbose)
			printf("SNMP::process_PDU: error-index is not integer\n");
		return false;
	}
	b->skip_bytes(pdu_length);

	// varbind list sequence
	uint8_t type_vb_list = b->get_byte();
	if (type_vb_list != 0x30) {
		if (verbose)
			printf("SNMP::process_PDU: expecting varbind list sequence, got %02x\n", type_vb_list);
		return false;
	}
	uint8_t len_vb_list = b->get_byte();

	if (len_vb_list) {
		block temp(b->get_bytes(len_vb_list));

		while(temp.is_empty() == false) {
			uint8_t seq_type   = temp.get_byte();
			uint8_t seq_length = temp.get_byte();

			block seq_data(temp.get_bytes(seq_length));

			if (seq_type == 0x30)  // sequence
				process_BER(&seq_data, oids_req, is_getnext, 0);
			else {
				if (verbose)
					printf("SNMP: unexpected/invalid type %02x\n", seq_type);
				return false;
			}
		}
	}

	return true;
}

bool snmp::process_BER(block *const b, oid_req_t *const oids_req, const bool is_getnext, const int is_top)
{
	bool first_integer   = true;
	bool first_octet_str = true;

	while(b->is_empty() == false) {
		uint8_t type   = b->get_byte();
		uint8_t length = b->get_byte();

		block temp(b->get_bytes(length));

		if (type == 0x02) {  // integer
			if (is_top && first_integer)
				oids_req->version = get_INTEGER(&temp);

			first_integer = false;
		}
		else if (type == 0x04) {  // octet string
			if (is_top && first_octet_str)
				oids_req->community = std::string(reinterpret_cast<const char *>(temp.get_data()), length);

			first_octet_str = false;
		}
		else if (type == 0x05) {  // null
			// ignore for now
		}
		else if (type == 0x06) {  // object identifier
			std::string oid_out;

			if (!get_OID(&temp, &oid_out))
				return false;

			if (is_getnext) {
				std::string oid_next = sd->find_next_oid(oid_out);

				if (oid_next.empty()) {
					oids_req->err     = 2;
					oids_req->err_idx = 1;
				}
				else {
					oids_req->oids.push_back(oid_next);
				}
			}
			else {
				oids_req->oids.push_back(oid_out);
			}
		}
		else if (type == 0x30) {  // sequence
			if (!process_BER(&temp, oids_req, is_getnext, is_top - 1))
				return false;
		}
		else if (type == 0xa0) {  // GetRequest PDU
			if (!process_PDU(&temp, oids_req, is_getnext))
				return false;
		}
		else if (type == 0xa1) {  // GetNextRequest PDU
			if (!process_PDU(&temp, oids_req, true))
				return false;
		}
		else if (type == 0xa3) {  // SetRequest PDU
			if (!process_PDU(&temp, oids_req, is_getnext))
				return false;
		}
		else {
			if (verbose)
				printf("SNMP: invalid type %02x\n", type);
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

		if (verbose)
			printf("SNMP requested: %s\n", e.c_str());

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
			if (verbose)
				printf("SNMP: requested %s not found, returning null\n", e.c_str());

			// FIXME snmp_null?
			varbind->add(new snmp_null());
		}
	}

	auto rc = se->get_payload();
	*packet_out  = rc.first;
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
               oid_req_t or_;

               handle->read(buffer, rc);

	       block b(buffer, rc, false);
	       if (!process_BER(&b, &or_, false, 2)) {
		       if (verbose)
			       printf("Processing packet failed\n");
		       return;
	       }

               uint8_t *packet_out  = nullptr;
               size_t   output_size = 0;
               gen_reply(or_, &packet_out, &output_size);
               if (output_size) {
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
#if (!defined(ARDUINO) || defined(ESP32)) && !defined(__MINGW32__)
        pollfd fds[] { { fd, POLLIN, 0 } };
#endif

	while(!*stop_flag) {
#if !defined(ARDUINO) || defined(ESP32)
                sockaddr_in clientaddr   {   };
                socklen_t   len          { sizeof clientaddr };

#if !defined(__MINGW32__)
                if (poll(fds, 1, 100) == 0)
                        continue;
#endif

                int rc = recvfrom(fd, reinterpret_cast<char *>(buffer), SNMP_RECV_BUFFER_SIZE, 0, reinterpret_cast<sockaddr *>(&clientaddr), &len);
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

			block b(buffer, rc, false);
			try {
				if (!process_BER(&b, &or_, false, 2))
					continue;
			}
			catch(const std::runtime_error & re) {
				if (verbose)
					printf("Processing packet failed: %s\n", re.what());
				continue;
			}

			uint8_t *packet_out  = nullptr;
			size_t   output_size = 0;
			gen_reply(or_, &packet_out, &output_size);
			if (output_size) {
#if !defined(ARDUINO) || defined(ESP32)
				(void)sendto(fd, reinterpret_cast<char *>(packet_out), output_size, 0, reinterpret_cast<sockaddr *>(&clientaddr), len);
#else
				handle->beginPacket(handle->remoteIP(), handle->remotePort());
				handle->write(packet_out, output_size);
				handle->endPacket();
#endif
			}

			delete [] packet_out;
		}
	}
}
#endif
