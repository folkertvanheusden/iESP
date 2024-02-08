#ifdef ESP32
#include <Arduino.h>
#endif
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#ifndef ESP32
#include <poll.h>
#endif
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#ifdef ESP32
#ifndef SOL_TCP
#define SOL_TCP 6
#endif
#endif

#include "iscsi-pdu.h"
#include "log.h"
#include "server.h"
#include "utils.h"


extern std::atomic_bool stop;

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
	if (listen_fd == -1) {
		DOLOG("server::begin: failed to create socket: %s\n", strerror(errno));
		return false;
	}

        int reuse_addr = 1;
        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(&reuse_addr), sizeof reuse_addr) == -1) {
		DOLOG("server::begin: failed to set socket to reuse address: %s\n", strerror(errno));
		return false;
	}

#ifdef linux
	int q_size = SOMAXCONN;
	if (setsockopt(listen_fd, SOL_TCP, TCP_FASTOPEN, &q_size, sizeof q_size)) {
		DOLOG("server::begin: failed to set \"TCP fast open\": %s\n", strerror(errno));
		return false;
	}
#endif

        sockaddr_in server_addr { };
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(listen_port);
	if (inet_aton(listen_ip.c_str(), &reinterpret_cast<sockaddr_in *>(&server_addr)->sin_addr) == 0) {
		DOLOG("server::begin: failed to translate listen address (%s): %s\n", listen_ip.c_str(), strerror(errno));
		return false;
	}

        if (bind(listen_fd, reinterpret_cast<sockaddr *>(&server_addr), sizeof server_addr) == -1) {
		DOLOG("server::begin: failed to bind socket to %s:%d: %s\n", listen_ip.c_str(), listen_port, strerror(errno));
		return false;
	}

        if (listen(listen_fd, 4) == -1) {
		DOLOG("server::begin: failed to setup listen queue: %s\n", strerror(errno));
                return false;
	}

	return true;
}

iscsi_pdu_bhs *server::receive_pdu(const int fd, session **const s)
{
	if (*s == nullptr)
		*s = new session();

#ifndef ESP32
	pollfd fds[] { { fd, POLLIN, 0 } };

	for(;;) {
		int rc = poll(fds, 1, 100);
		if (rc == -1) {
			DOLOG("server::receive_pdu: poll failed with error %s\n", strerror(errno));
			return nullptr;
		}

		if (stop == true) {
			DOLOG("server::receive_pdu: abort due external stop\n");
			return nullptr;
		}

		if (rc >= 1)
			break;
	}
#endif

	uint8_t pdu[48] { 0 };
	if (READ(fd, pdu, sizeof pdu) == -1) {
		DOLOG("server::receive_pdu: PDU receive error\n");
		return nullptr;
	}

	bytes_recv += sizeof pdu;

	iscsi_pdu_bhs bhs;
	if (bhs.set(*s, pdu, sizeof pdu) == false) {
		DOLOG("server::receive_pdu: BHS validation error\n");
		return nullptr;
	}

#ifdef ESP32
//	slow!
//	Serial.print(millis());
//	Serial.print(' ');
//	Serial.println(pdu_opcode_to_string(bhs.get_opcode()).c_str());
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
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_r2t:
			pdu_obj = new iscsi_pdu_scsi_r2t();
			break;
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_scsi_taskman:
			pdu_obj = new iscsi_pdu_taskman_request();
			break;
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_scsi_data_out:
			pdu_obj = new iscsi_pdu_scsi_data_out();
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

#if defined(ESP32) || !defined(NDEBUG)
		if (bhs.get_opcode() == iscsi_pdu_bhs::iscsi_bhs_opcode::o_login_req) {
			auto initiator = reinterpret_cast<iscsi_pdu_login_request *>(pdu_obj)->get_initiator();
			if (initiator.has_value()) {
#ifdef ESP32
				Serial.print(F("Initiator: "));
				Serial.println(initiator.value().c_str());
#else
				DOLOG("server::receive_pdu: initiator: %s\n", initiator.value().c_str());
#endif
			}
		}
#endif

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
			delete [] ahs_temp;

			bytes_recv += ahs_len;
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

			bytes_recv += padded_data_length;
		}
		
		if (!ok) {
			DOLOG("server::receive_pdu: cannot return PDU\n");
			delete pdu_obj;
			pdu_obj = nullptr;
		}
	}

	return pdu_obj;
}

bool server::push_response(const int fd, session *const s, iscsi_pdu_bhs *const pdu, iscsi_response_parameters *const parameters)
{
	bool ok = true;

	std::optional<iscsi_response_set> response_set;

	if (pdu->get_opcode() == iscsi_pdu_bhs::iscsi_bhs_opcode::o_scsi_data_out) {
		auto     pdu_data_out = reinterpret_cast<iscsi_pdu_scsi_data_out *>(pdu);
		uint32_t offset       = pdu_data_out->get_BufferOffset();
		auto     data         = pdu_data_out->get_data();
		uint32_t TTT          = pdu_data_out->get_TTT();
		bool     F            = pdu_data_out->get_F();
		auto     session      = s->get_r2t_sesion(TTT);

		if (session == nullptr) {
			DOLOG("server::push_response: DATA-OUT PDU references unknown TTT (%08x)\n", TTT);
			if (data.has_value())
				delete [] data.value().first;
			return false;
		}
		else if (data.has_value() && data.value().second > 0) {
			auto block_size = b->get_block_size();
			DOLOG("server::push_response: writing %zu bytes to offset LBA %zu + offset %u => %zu (in bytes)\n", data.value().second, session->buffer_lba, offset, session->buffer_lba * block_size + offset);

			assert((offset % block_size) == 0);
			assert((data.value().second % block_size) == 0);

			bool rc = b->write(session->buffer_lba + offset / block_size, data.value().second / block_size, data.value().first);
			delete [] data.value().first;

			if (rc == false) {
				DOLOG("server::push_response: DATA-OUT problem writing to backend\n");
				return rc;
			}

			session->bytes_done += data.value().second;
			session->bytes_left -= data.value().second;
		}
		else if (!F) {
			DOLOG("server::push_response: DATA-OUT PDU has no data?\n");
			return false;
		}

		// create response
		if (F) {
			DOLOG("server::push-response: end of batch\n");

			iscsi_pdu_scsi_cmd response;
			response.set(s, session->PDU_initiator.data, session->PDU_initiator.n);  // TODO check for false
			response_set = response.get_response(s, parameters, session->bytes_left);

			if (session->bytes_left == 0) {
				DOLOG("server::push-response: end of task\n");

				s->remove_r2t_session(TTT);
			}
			else {
				DOLOG("server::push-response: ask for more (%u bytes left)\n", session->bytes_left);
				// send 0x31 for range
				iscsi_pdu_scsi_cmd temp;
				temp.set(s, session->PDU_initiator.data, session->PDU_initiator.n);

				auto *response = new iscsi_pdu_scsi_r2t /* 0x31 */;
				response->set(s, temp, TTT, session->bytes_done, session->bytes_left);  // TODO check for false
				// ^ ADD TO RESPONSE SET TODO

				response_set.value().responses.push_back(response);
			}
		}
	}
	else {
		response_set = pdu->get_response(s, parameters);
	}

	if (response_set.has_value() == false) {
		DOLOG("server::push_response: no response from PDU\n");
		return true;
	}

	for(auto & pdu_out: response_set.value().responses) {
		for(auto & blobs: pdu_out->get()) {
			if (blobs.data == nullptr) {
				ok = false;
				DOLOG("server::push_response: PDU did not emit data bundle\n");
			}

			assert((blobs.n & 3) == 0);

			if (ok) {
				ok = WRITE(fd, blobs.data, blobs.n) != -1;
				if (!ok)
					DOLOG("server::push_response: sending PDU to peer failed (%s)\n", strerror(errno));
				bytes_send += blobs.n;
			}

			delete [] blobs.data;
		}

		delete pdu_out;
	}

	// e.g. for READ_xx (as buffering may be RAM-wise too costly)
	if (response_set.value().to_stream.has_value()) {
		auto & stream_parameters = response_set.value().to_stream.value();

		DOLOG("server::push_response: stream %u sectors, LBA: %zu\n", stream_parameters.n_sectors, size_t(stream_parameters.lba));

		iscsi_pdu_scsi_cmd reply_to;
		auto temp = pdu->get_raw();
		reply_to.set(s, temp.data, temp.n);
		delete [] temp.data;

	        auto use_pdu_data_size = stream_parameters.n_sectors * 512;
		if (use_pdu_data_size > size_t(reply_to.get_ExpDatLen())) {
			DOLOG("server::push_response: requested less (%zu) than wat is available (%u)\n", size_t(reply_to.get_ExpDatLen()), use_pdu_data_size);
			use_pdu_data_size = reply_to.get_ExpDatLen();
		}

		blob_t buffer { nullptr, 0 };
#ifdef ESP32
		size_t heap_free = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
		if (heap_free >= 5120)
			buffer.n = std::min((heap_free - 2048) / 2, s->get_ack_interval());
		if (buffer.n < 512)
			buffer.n = 512;
		buffer.data = new uint8_t[buffer.n]();
#else
		buffer.n    = std::max(512u, s->get_ack_interval());
		buffer.data = new uint8_t[buffer.n]();
#endif
		uint32_t block_group_size = buffer.n / 512;

		uint32_t offset = 0;

		for(uint32_t block_nr = 0; block_nr < stream_parameters.n_sectors; block_nr += block_group_size) {
			uint32_t n_left = std::min(block_group_size, stream_parameters.n_sectors - block_nr);
			buffer.n = n_left * 512;

			uint64_t cur_block_nr = block_nr + stream_parameters.lba;
			DOLOG("server::push_response: reading %u block(s) %zu from backend\n", n_left, size_t(cur_block_nr));
			if (b->read(cur_block_nr, n_left, buffer.data) == false) {
				DOLOG("server::push_response: reading %u block(s) %zu from backend failed\n", n_left, size_t(cur_block_nr));
				ok = false;
				break;
			}

			blob_t out = iscsi_pdu_scsi_data_in::gen_data_in_pdu(s, reply_to, buffer, use_pdu_data_size, offset);

			if (WRITE(fd, out.data, out.n) == -1) {
				delete [] out.data;
				DOLOG("server::push_response: problem sending block %zu to initiator\n", size_t(cur_block_nr));
				ok = false;
				break;
			}
			bytes_send += out.n;

			delete [] out.data;

			offset += buffer.n;
		}

		delete [] buffer.data;
	}

	DOLOG(" ---\n");

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
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_r2t:
			return new iscsi_response_parameters_r2t(ses);
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_scsi_taskman:
			return new iscsi_response_parameters_taskman(ses);
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_scsi_data_out:
			return new iscsi_response_parameters_data_out(ses);
		default:
			DOLOG("server::select_parameters: opcode %02xh not implemented\n", pdu->get_opcode());
			break;
	}

	return nullptr;
}

void server::handler()
{
	scsi scsi_dev(b);

#ifndef ESP32
	pollfd fds[] { { listen_fd, POLLIN, 0 } };
#endif

	while(!stop) {
#ifndef ESP32
		while(!stop) {
			int rc = poll(fds, 1, 100);
			if (rc == -1) {
				DOLOG("server::handler: poll failed with error %s\n", strerror(errno));
				break;
			}

			if (rc >= 1)
				break;
		}
#endif

		if (stop)
			break;

		int fd = accept(listen_fd, nullptr, nullptr);
		if (fd == -1) {
			DOLOG("server::handler: accept() failed: %s\n", strerror(errno));
			continue;
		}

		std::string endpoint = get_endpoint_name(fd);

		int flags = 1;
#if defined(__FreeBSD__)
		if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void *)&flags, sizeof(flags)) == -1)
#else
		if (setsockopt(fd, SOL_TCP, TCP_NODELAY, (void *)&flags, sizeof(flags)) == -1)
#endif
			DOLOG("server::handler: cannot disable Nagle algorithm\n");

#ifdef ESP32
		Serial.printf("new session with %s\r\n", endpoint.c_str());
		uint32_t pdu_count   = 0;
		auto     prev_output = millis();
		auto     start       = prev_output;
		unsigned long busy   = 0;
		const long interval  = 5000;
#else
		DOLOG("server::handler: new session with %s\n", endpoint.c_str());
#endif

		session *s  = nullptr;
		bool     ok = true;

		do {
			iscsi_pdu_bhs *pdu = receive_pdu(fd, &s);
			if (!pdu) {
				DOLOG("server::handler: no PDU received, aborting socket connection\n");
				break;
			}

#ifdef ESP32
			auto tx_start = micros();
#endif
			auto parameters = select_parameters(pdu, s, &scsi_dev);
			if (parameters) {
				push_response(fd, s, pdu, parameters);
				delete parameters;
			}
			else {
				ok = false;
			}

			delete pdu;
#ifdef ESP32
			auto tx_end = micros();
			busy += tx_end - tx_start;
#endif

#ifdef ESP32
			pdu_count++;
			auto now = millis();
			auto took = now - prev_output;
			if (took >= interval) {
				prev_output = now;
				double   dtook = took / 1000.;
				uint64_t bytes_read    = 0;
				uint64_t bytes_written = 0;
				uint64_t n_syncs       = 0;
				b->get_and_reset_stats(&bytes_read, &bytes_written, &n_syncs);
				Serial.printf("%ld] PDU/s: %.2f (%zu), send: %" PRIu64 " (%.2f/s), recv: %" PRIu64 " (%.2f/s), written: %.2f/s, read: %.2f/s, syncs: %.2f/s, load: %.2f%%\r\n", now, pdu_count / dtook, pdu_count, bytes_send, bytes_send / dtook, bytes_recv, bytes_recv / dtook, bytes_written / dtook, bytes_read / dtook, n_syncs / dtook, busy * 0.1 / interval);
				pdu_count  = 0;
				bytes_send = 0;
				bytes_recv = 0;
				busy       = 0;
			}
#endif
		}
		while(ok);
#ifdef ESP32
		Serial.println(F("session finished"));
#else
		DOLOG("session finished\n");
#endif

		b->sync();

		close(fd);
		delete s;
	}
}
