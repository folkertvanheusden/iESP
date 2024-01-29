#ifdef ESP32
#include <Arduino.h>
#endif
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "iscsi-pdu.h"
#include "log.h"
#include "server.h"
#include "utils.h"


server::server(backend *const b, const std::string & listen_ip, const int listen_port):
	b(b),
	listen_ip(listen_ip), listen_port(listen_port)
{
}

server::~server()
{
	close(listen_fd);
}

bool server::begin()
{
	// setup listening socket for viewers
	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd == -1)
		return false;

        int reuse_addr = 1;
        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(&reuse_addr), sizeof reuse_addr) == -1)
		return false;

#ifdef linux
	int q_size = SOMAXCONN;
	if (setsockopt(listen_fd, SOL_TCP, TCP_FASTOPEN, &q_size, sizeof q_size))
		return false;
#endif

        sockaddr_in server_addr { };
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(listen_port);
	if (inet_aton(listen_ip.c_str(), &reinterpret_cast<sockaddr_in *>(&server_addr)->sin_addr) == 0)
		return false;

        if (bind(listen_fd, reinterpret_cast<sockaddr *>(&server_addr), sizeof server_addr) == -1)
		return false;

        if (listen(listen_fd, 4) == -1)
                return false;

	return true;
}

iscsi_pdu_bhs *server::receive_pdu(const int fd, session **const s)
{
	if (*s == nullptr)
		*s = new session();

	uint8_t pdu[48] { 0 };
	if (READ(fd, pdu, sizeof pdu) == -1) {
		DOLOG("server::receive_pdu: PDU receive error\n");
		return nullptr;
	}

	iscsi_pdu_bhs bhs;
	if (bhs.set(*s, pdu, sizeof pdu) == false) {
		DOLOG("server::receive_pdu: BHS validation error\n");
		return nullptr;
	}

#ifdef ESP32
	Serial.print(millis());
	Serial.print(' ');
	Serial.println(pdu_opcode_to_string(bhs.get_opcode()).c_str());
#else
	DOLOG("opcode: %02xh / %s\n", bhs.get_opcode(), pdu_opcode_to_string(bhs.get_opcode()).c_str());
#endif

	iscsi_pdu_bhs *pdu_obj = nullptr;

	switch(bhs.get_opcode()) {
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_login_req:
			pdu_obj = new iscsi_pdu_login_request();
			break;
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_scsi_cmd:
			pdu_obj = new iscsi_pdu_scsi_cmd();
			break;
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_nop_out:
			pdu_obj = new iscsi_pdu_nop_out();
			break;
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_text_req:
			pdu_obj = new iscsi_pdu_text_request();
			break;
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_logout_req:
			pdu_obj = new iscsi_pdu_logout_request();
			break;
		default:
			DOLOG("server::receive_pdu: opcode %02xh not implemented\n", bhs.get_opcode());
			break;
	}

	if (pdu_obj) {
		bool ok = true;

		if (pdu_obj->set(*s, pdu, sizeof pdu) == false) {
			ok = false;
			DOLOG("server::receive_pdu: initialize PDU: validation failed\n");
		}

		size_t ahs_len = pdu_obj->get_ahs_length();
		if (ahs_len) {
			DOLOG("server::receive_pdu: read %zu ahs bytes\n", ahs_len);

			uint8_t *ahs_temp = new uint8_t[ahs_len]();
			if (READ(fd, ahs_temp, ahs_len) == -1) {
				ok = false;
				DOLOG("server::receive_pdu: AHS receive error\n");
			}
			else {
				pdu_obj->set_ahs_segment({ ahs_temp, ahs_len });
			}
			delete ahs_temp;
		}

		size_t data_length = pdu_obj->get_data_length();
		if (data_length) {
			size_t padded_data_length = (data_length + 3) & ~3;

			DOLOG("server::receive_pdu: read %zu data bytes (%zu with padding)\n", data_length, padded_data_length);

			uint8_t *data_temp = new uint8_t[padded_data_length]();
			if (READ(fd, data_temp, padded_data_length) == -1) {
				ok = false;
				DOLOG("server::receive_pdu: data receive error\n");
			}
			else {
				pdu_obj->set_data({ data_temp, data_length });
			}
			delete [] data_temp;
		}
		
		if (!ok) {
			DOLOG("server::receive_pdu: cannot return PDU\n");
			delete pdu_obj;
			pdu_obj = nullptr;
		}
	}

	return pdu_obj;
}

bool server::push_response(const int fd, iscsi_pdu_bhs *const pdu, iscsi_response_parameters *const parameters)
{
	auto response_set = pdu->get_response(parameters, pdu->get_data());
	if (response_set.has_value() == false) {
		DOLOG("server::push_response: no response from PDU\n");
		return false;
	}

	bool ok = true;

	for(auto & pdu_out: response_set.value().responses) {
		auto iscsi_reply = pdu_out->get();
		if (iscsi_reply.first == nullptr) {
			ok = false;
			DOLOG("server::push_response: PDU did not emit data bundle\n");
		}

		assert((iscsi_reply.second & 3) == 0);

		if (ok) {
			printf("SENDING %zu bytes\n", iscsi_reply.second);
			ok = WRITE(fd, iscsi_reply.first, iscsi_reply.second) != -1;
			if (!ok)
				DOLOG("server::push_response: sending PDU to peer failed\n");
		}

		delete [] iscsi_reply.first;

		delete pdu_out;

		DOLOG(" ---\n");
	}

	return ok;
}

iscsi_response_parameters *server::select_parameters(iscsi_pdu_bhs *const pdu, session *const ses, scsi *const sd)
{
	switch(pdu->get_opcode()) {
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_login_req:
			return new iscsi_response_parameters_login_req(ses);
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_scsi_cmd:
			return new iscsi_response_parameters_scsi_cmd(ses, sd);
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_nop_out:
			return new iscsi_response_parameters_nop_out(ses);
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_text_req:
			return new iscsi_response_parameters_text_req(ses, listen_ip, listen_port);
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_logout_req:
			return new iscsi_response_parameters_logout_req(ses);
		default:
			DOLOG("server::select_parameters: opcode %02xh not implemented\n", pdu->get_opcode());
			break;
	}

	return nullptr;
}

void server::handler()
{
	scsi scsi_dev(b);

	for(;;) {
		int fd = accept(listen_fd, nullptr, nullptr);
		if (fd == -1)
			continue;

		session *s  = nullptr;
		bool     ok = true;

		// TODO: handle R2T
		do {
			iscsi_pdu_bhs *pdu = receive_pdu(fd, &s);
			if (!pdu)
				break;

			auto parameters = select_parameters(pdu, s, &scsi_dev);
			if (parameters) {
				push_response(fd, pdu, parameters);
				delete parameters;
			}
			else {
				ok = false;
			}

			delete pdu;
		}
		while(ok);

		close(fd);

		delete s;
	}
}
