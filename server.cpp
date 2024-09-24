#if defined(ESP32) || defined(RP2040W) || defined(TEENSY4_1)
#include <Arduino.h>
#ifndef TEENSY4_1
#include <WiFi.h>
#endif
#endif
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#if !defined(ESP32) && !defined(RP2040W) && !defined(TEENSY4_1)
#include <poll.h>
#endif
#include <thread>
#include <unistd.h>
#if !defined(RP2040W) && !defined(TEENSY4_1)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif
#include <sys/types.h>

#include "iscsi-pdu.h"
#include "log.h"
#include "server.h"
#include "utils.h"


extern std::atomic_bool stop;

server::server(scsi *const s, com *const c, iscsi_stats_t *is):
	s(s),
	c(c),
	is(is)
{
}

server::~server()
{
}

std::pair<iscsi_pdu_bhs *, bool> server::receive_pdu(com_client *const cc, session **const s)
{
	if (*s == nullptr)
		*s = new session(cc);

	uint8_t pdu[48] { 0 };
	if (cc->recv(pdu, sizeof pdu) == false) {
		errlog("server::receive_pdu: PDU receive error");
		return { nullptr, false };
	}

	(*s)->add_bytes_rx(sizeof pdu);
	is->iscsiSsnRxDataOctets += sizeof pdu;

	iscsi_pdu_bhs bhs;
	if (bhs.set(*s, pdu, sizeof pdu) == false) {
		errlog("server::receive_pdu: BHS validation error");
		return { nullptr, false };
	}

#if defined(ESP32) || defined(RP2040W)
//	slow!
//	Serial.print(millis());
//	Serial.print(' ');
//	Serial.println(pdu_opcode_to_string(bhs.get_opcode()).c_str());
#else
	DOLOG("opcode: %02xh / %s\n", bhs.get_opcode(), pdu_opcode_to_string(bhs.get_opcode()).c_str());
#endif

	iscsi_pdu_bhs *pdu_obj   = nullptr;
	bool           pdu_error = false;

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
			errlog("server::receive_pdu: opcode %02xh not implemented", bhs.get_opcode());
			pdu_obj = new iscsi_pdu_bhs();
			pdu_error = true;
			break;
	}

	if (pdu_obj) {
		bool ok = true;

		if (pdu_obj->set(*s, pdu, sizeof pdu) == false) {
			ok = false;
			errlog("server::receive_pdu: initialize PDU: validation failed");
		}

		if (bhs.get_opcode() == iscsi_pdu_bhs::iscsi_bhs_opcode::o_login_req) {
			is->iscsiTgtLoginAccepts++;
#if defined(ESP32) || !defined(NDEBUG)
			auto initiator = reinterpret_cast<iscsi_pdu_login_request *>(pdu_obj)->get_initiator();
			if (initiator.has_value()) {
#ifdef ESP32
				Serial.print(F("Initiator: "));
				Serial.println(initiator.value().c_str());
#else
				DOLOG("server::receive_pdu: initiator: %s\n", initiator.value().c_str());
#endif
			}
#endif
		}

		if (bhs.get_opcode() == iscsi_pdu_bhs::iscsi_bhs_opcode::o_logout_req)
			is->iscsiTgtLogoutNormals++;

		size_t ahs_len = pdu_obj->get_ahs_length();
		if (ahs_len) {
			DOLOG("server::receive_pdu: read %zu ahs bytes\n", ahs_len);

			uint8_t *ahs_temp = new uint8_t[ahs_len]();
			if (cc->recv(ahs_temp, ahs_len) == false) {
				ok = false;
				errlog("server::receive_pdu: AHS receive error");
			}
			else {
				pdu_obj->set_ahs_segment({ ahs_temp, ahs_len });
			}
			delete [] ahs_temp;

			(*s)->add_bytes_rx(ahs_len);
			is->iscsiSsnRxDataOctets += ahs_len;
		}

		size_t data_length = pdu_obj->get_data_length();
		if (data_length) {
			size_t padded_data_length = (data_length + 3) & ~3;

			DOLOG("server::receive_pdu: read %zu data bytes (%zu with padding)\n", data_length, padded_data_length);

			uint8_t *data_temp = new uint8_t[padded_data_length]();
			if (cc->recv(data_temp, padded_data_length) == false) {
				ok = false;
				errlog("server::receive_pdu: data receive error");
			}
			else {
				pdu_obj->set_data({ data_temp, data_length });
			}
			delete [] data_temp;

			(*s)->add_bytes_rx(padded_data_length);
			is->iscsiSsnRxDataOctets += padded_data_length;
		}
		
		if (!ok) {
			errlog("server::receive_pdu: cannot return PDU");
			delete pdu_obj;
			pdu_obj = nullptr;
		}
	}


	return { pdu_obj, pdu_error };
}

bool server::push_response(com_client *const cc, session *const ses, iscsi_pdu_bhs *const pdu, scsi *const sd)
{
	bool ok = true;

	std::optional<iscsi_response_set> response_set;

	if (pdu->get_opcode() == iscsi_pdu_bhs::iscsi_bhs_opcode::o_scsi_data_out) {
		auto     pdu_data_out = reinterpret_cast<iscsi_pdu_scsi_data_out *>(pdu);
		uint32_t offset       = pdu_data_out->get_BufferOffset();
		auto     data         = pdu_data_out->get_data();
		uint32_t TTT          = pdu_data_out->get_TTT();
		bool     F            = pdu_data_out->get_F();
		auto     session      = ses->get_r2t_sesion(TTT);

		if (TTT == 0xffffffff) {  // unsollicited data
			TTT = pdu_data_out->get_Itasktag();
		}

		if (session == nullptr) {
			DOLOG("server::push_response: DATA-OUT PDU references unknown TTT (%08x)", TTT);
			if (data.has_value())
				delete [] data.value().first;
			return false;
		}
		else if (data.has_value() && data.value().second > 0) {
			auto block_size = s->get_block_size();
			DOLOG("server::push_response: writing %zu bytes to offset LBA %zu + offset %u => %zu (in bytes)\n", data.value().second, session->buffer_lba, offset, session->buffer_lba * block_size + offset);

			assert((offset % block_size) == 0);
			assert((data.value().second % block_size) == 0);

			auto rc = s->write(session->buffer_lba + offset / block_size, data.value().second / block_size, data.value().first);
			delete [] data.value().first;

			if (rc != scsi::rw_ok) {
				errlog("server::push_response: DATA-OUT problem writing to backend (%d)", rc);
				return false;
			}

			if (session->fua) {
				if (s->sync() == false) {
					errlog("server::push_response: DATA-OUT problem syncing data to backend");
					return false;
				}
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
			DOLOG("server::push_response: end of batch\n");

			iscsi_pdu_scsi_cmd response;
			if (response.set(ses, session->PDU_initiator.data, session->PDU_initiator.n) == false) {
				errlog("server::push_response: response.set failed");
				return false;
			}
			response_set = response.get_response(ses, sd, session->bytes_left);

			if (session->bytes_left == 0) {
				DOLOG("server::push_response: end of task\n");

				ses->remove_r2t_session(TTT);
			}
			else {
				DOLOG("server::push_response: ask for more (%u bytes left)\n", session->bytes_left);
				// send 0x31 for range
				iscsi_pdu_scsi_cmd temp;
				temp.set(ses, session->PDU_initiator.data, session->PDU_initiator.n);

				auto *response = new iscsi_pdu_scsi_r2t() /* 0x31 */;
				if (response->set(ses, temp, TTT, session->bytes_done, session->bytes_left) == false) {
					errlog("server::push_response: response->set failed");
					delete response;
					return false;
				}

				response_set.value().responses.push_back(response);
			}
		}
	}
	else {
		response_set = pdu->get_response(ses, sd);
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
				ok = cc->send(blobs.data, blobs.n);
				if (!ok)
					errlog("server::push_response: sending PDU to peer failed (%s)", strerror(errno));
				ses->add_bytes_tx(blobs.n);
				is->iscsiSsnTxDataOctets += blobs.n;
			}

			delete [] blobs.data;
		}

		delete pdu_out;
	}

	// e.g. for READ_xx (as buffering may be RAM-wise too costly (on the ESP32))
	if (response_set.value().to_stream.has_value()) {
		auto & stream_parameters = response_set.value().to_stream.value();

		DOLOG("server::push_response: stream %u sectors, LBA: %zu\n", stream_parameters.n_sectors, size_t(stream_parameters.lba));

		iscsi_pdu_scsi_cmd reply_to;
		auto temp = pdu->get_raw();
		reply_to.set(ses, temp.data, temp.n);
		delete [] temp.data;

	        uint64_t use_pdu_data_size = uint64_t(stream_parameters.n_sectors) * s->get_block_size();
		if (use_pdu_data_size > reply_to.get_ExpDatLen()) {
			DOLOG("server::push_response: requested less (%u) than wat is available (%" PRIu64 ")\n", reply_to.get_ExpDatLen(), use_pdu_data_size);
			use_pdu_data_size = reply_to.get_ExpDatLen();
		}

		blob_t buffer       { nullptr, 0 };
		auto   ack_interval = ses->get_ack_interval();
#ifdef ESP32
		size_t heap_free = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
		if (heap_free >= 5120) {
			size_t heap_allowed = (heap_free - 2048) / 4;

			if (ack_interval.has_value())
				buffer.n = std::min(heap_allowed, size_t(ack_interval.value()));
			else
				buffer.n = heap_allowed;
		}
#elif defined(RP2040W)
		if (ack_interval.has_value())
			buffer.n = std::min(uint32_t(4096), ack_interval.value());
		else
			buffer.n = 4096;
#elif defined(TEENSY4_1)
		if (ack_interval.has_value())
			buffer.n = std::min(uint32_t(16384), ack_interval.value());
		else
			buffer.n = 16384;
#else
		if (ack_interval.has_value())
			buffer.n = std::max(uint32_t(s->get_block_size()), ack_interval.value());
		else
			buffer.n = std::max(uint32_t(s->get_block_size()), uint32_t(65536));  // 64 kB, arbitrarily chosen
#endif
		if (buffer.n < s->get_block_size())
			buffer.n = s->get_block_size();
		buffer.data = new uint8_t[buffer.n]();
		uint32_t block_group_size = buffer.n / s->get_block_size();

		uint32_t offset = 0;

		bool low_ram = false;

		for(uint32_t block_nr = 0; block_nr < stream_parameters.n_sectors;) {
			uint32_t n_left = low_ram ? 1 : std::min(block_group_size, stream_parameters.n_sectors - block_nr);
			buffer.n = n_left * s->get_block_size();

			if (offset < use_pdu_data_size)
				buffer.n = std::min(buffer.n, size_t(use_pdu_data_size - offset));
			else
				buffer.n = 0;

			uint64_t cur_block_nr = block_nr + stream_parameters.lba;
			DOLOG("server::push_response: reading %u block(s) nr %zu from backend\n", n_left, size_t(cur_block_nr));
			if (buffer.n > 0) {
				auto rc = s->read(cur_block_nr, n_left, buffer.data);

				if (rc != scsi::rw_ok) {
					errlog("server::push_response: reading %u block(s) %zu from backend failed (%d)", n_left, size_t(cur_block_nr), rc);
					ok = false;
					break;
				}
			}

			blob_t out = iscsi_pdu_scsi_data_in::gen_data_in_pdu(ses, reply_to, buffer, use_pdu_data_size, offset);

			if (out.n == 0) {  // gen_data_in_pdu could not allocate memory
				low_ram = true;
				delete [] buffer.data;
				buffer.n = s->get_block_size();
				buffer.data = new uint8_t[buffer.n]();
				errlog("Low on memory: %zu bytes failed", buffer.n);
			}
			else {
				if (cc->send(out.data, out.n) == false) {
					delete [] out.data;
					errlog("server::push_response: problem sending block %zu to initiator", size_t(cur_block_nr));
					ok = false;
					break;
				}

				delete [] out.data;

				ses->add_bytes_tx(out.n);
				offset += buffer.n;
				block_nr += n_left;
				is->iscsiSsnTxDataOctets += out.n;
			}

			if (buffer.n == 0)
				break;
		}

		delete [] buffer.data;
	}

	DOLOG(" ---\n");

	return ok;
}

void server::handler()
{
	std::vector<std::pair<std::thread *, std::atomic_bool *> > threads;

	while(!stop) {
		com_client *cc = c->accept();
		if (cc == nullptr) {
			errlog("server::handler: accept() failed: %s", strerror(errno));
			continue;
		}

#if !defined(TEENSY4_1)
		for(size_t i=0; i<threads.size();) {
			if (*threads.at(i).second) {
				DOLOG("server::handler: thread cleaned up\n");
				threads.at(i).first->join();
				delete threads.at(i).first;
				delete threads.at(i).second;
				threads.erase(threads.begin() + i);
			}
			else {
				i++;
			}
		}

		std::atomic_bool *flag = new std::atomic_bool(false);

		std::thread *th = new std::thread([=, this]() {
#endif
			std::string endpoint = cc->get_endpoint_name();

#if defined(ESP32) || defined(RP2040W) || defined(TEENSY4_1)
			Serial.printf("new session with %s\r\n", endpoint.c_str());
#else
			DOLOG("server::handler: new session with %s\n", endpoint.c_str());
#endif
			auto     prev_output = get_millis();
			uint32_t pdu_count   = 0;
			auto     start       = prev_output;
			unsigned long busy   = 0;
			const long interval  = 5000;
			bool     first       = true;

			session *ses = nullptr;
			bool     ok  = true;

			do {
				auto incoming = receive_pdu(cc, &ses);
				iscsi_pdu_bhs *pdu = incoming.first;
				if (!pdu) {
					DOLOG("server::handler: no PDU received, aborting socket connection\n");
					is->iscsiInstSsnFailures++;
					break;
				}

				is->iscsiSsnCmdPDUs++;

				if (first) {
					first = false;
					ses->set_block_size(s->get_block_size());
				}

				auto tx_start = get_micros();

				if (incoming.second) {  // something wrong with the received PDU?
					errlog("server::handler: invalid PDU received");

					is->iscsiInstSsnFormatErrors++;

					std::optional<blob_t> reject = generate_reject_pdu(*pdu);
					if (reject.has_value() == false) {
						errlog("server::handler: cannot generate reject PDU");
						continue;
					}

					bool rc = cc->send(reject.value().data, reject.value().n);
					delete [] reject.value().data;
					if (rc == false) {
						errlog("server::handler: cannot transmit reject PDU");
						break;
					}
					is->iscsiSsnTxDataOctets += reject.value().n;

					DOLOG("server::handler: transmitted reject PDU\n");
				}
				else {
					ok = push_response(cc, ses, pdu, s);
					if (!ok)
						is->iscsiInstSsnFailures++;

					delete pdu;
				}

				auto tx_end = get_micros();
				busy += tx_end - tx_start;

				pdu_count++;
				auto now = get_millis();
				auto took = now - prev_output;
				if (took >= interval) {
					prev_output = now;
					double   dtook = took / 1000.;
					double   dkB   = dtook * 1024;
					uint64_t bytes_read    = 0;
					uint64_t bytes_written = 0;
					uint64_t n_syncs       = 0;
					uint64_t n_trims       = 0;
					s->get_and_reset_stats(&bytes_read, &bytes_written, &n_syncs, &n_trims);
#if defined(ARDUINO)
					Serial.printf("%.3f] PDU/s: %.2f, send: %.2f kB/s, recv: %.2f kB/s, written: %.2f kB/s, read: %.2f kB/s, syncs: %.2f/s, unmaps: %.2f/s, load: %.2f%%, mem: %" PRIu32 "\r\n", now / 1000., pdu_count / dtook, ses->get_bytes_tx() / dkB, ses->get_bytes_rx() / dkB, bytes_written / dkB, bytes_read / dkB, n_syncs / dtook, n_trims / dtook, busy * 0.1 / took, get_free_heap_space());
#else
					fprintf(stderr, "%.3f] PDU/s: %.2f, send: %.2f kB/s, recv: %.2f kB/s, written: %.2f kB/s, read: %.2f kB/s, syncs: %.2f/s, unmaps: %.2f/s, load: %.2f%%\n", now / 1000., pdu_count / dtook, ses->get_bytes_tx() / dkB, ses->get_bytes_rx() / dkB, bytes_written / dkB, bytes_read / dkB, n_syncs / dtook, n_trims / dtook, busy * 0.1 / took);
#endif
					pdu_count  = 0;
					ses->reset_bytes_rx();
					ses->reset_bytes_tx();
					busy       = 0;
				}
			}
			while(ok);
#if defined(ESP32) || defined(RP2040W)
			Serial.printf("session finished: %d\r\n", WiFi.status());
#else
			DOLOG("session finished\n");
#endif

			s->sync();
			if (s->locking_status() == scsi::l_locked)
				s->unlock_device();

			delete cc;
			delete ses;

#if !defined(TEENSY4_1)
			*flag = true;
		});

		threads.push_back({ th, flag });
#endif

#if defined(ESP32)
		Serial.printf("Heap space: %u\r\n", get_free_heap_space());
#endif
	}

#if !defined(TEENSY4_1)
	for(auto &e: threads) {
		if (e.second) {
			e.first->join();
			delete e.first;
			delete e.second;
		}
	}
#endif
}
