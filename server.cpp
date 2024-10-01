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

server::server(scsi *const s, com *const c, iscsi_stats_t *is, const std::string & target_name):
	s(s),
	c(c),
	is(is),
	target_name(target_name)
{
}

server::~server()
{
}

std::tuple<iscsi_pdu_bhs *, bool, uint64_t> server::receive_pdu(com_client *const cc, session **const ses)
{
	if (*ses == nullptr) {
		*ses = new session(cc, target_name);
		(*ses)->set_block_size(s->get_block_size());
	}

	uint8_t pdu[48] { 0 };
	if (cc->recv(pdu, sizeof pdu) == false) {
		DOLOG(logging::ll_info, "server::receive_pdu", cc->get_endpoint_name(), "PDU receive error");
		return { nullptr, false, 0 };
	}

	uint64_t tx_start = get_micros();

	(*ses)->add_bytes_rx(sizeof pdu);
	is->iscsiSsnRxDataOctets += sizeof pdu;

	iscsi_pdu_bhs bhs(*ses);
	if (bhs.set(pdu, sizeof pdu) == false) {
		DOLOG(logging::ll_debug, "server::receive_pdu", cc->get_endpoint_name(), "BHS validation error");
		return { nullptr, false, tx_start };
	}

#if defined(ESP32) || defined(RP2040W)
//	slow!
//	Serial.print(millis());
//	Serial.print(' ');
//	Serial.println(pdu_opcode_to_string(bhs.get_opcode()).c_str());
#else
	DOLOG(logging::ll_debug, "server::receive_pdu", cc->get_endpoint_name(), "opcode: %02xh / %s", bhs.get_opcode(), pdu_opcode_to_string(bhs.get_opcode()).c_str());
#endif

	iscsi_pdu_bhs *pdu_obj   = nullptr;
	bool           pdu_error = false;

	switch(bhs.get_opcode()) {
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_login_req:
			pdu_obj = new iscsi_pdu_login_request(*ses);
			break;
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_scsi_cmd:
			pdu_obj = new iscsi_pdu_scsi_cmd(*ses);
			break;
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_nop_out:
			pdu_obj = new iscsi_pdu_nop_out(*ses);
			break;
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_text_req:
			pdu_obj = new iscsi_pdu_text_request(*ses);
			break;
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_logout_req:
			pdu_obj = new iscsi_pdu_logout_request(*ses);
			break;
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_r2t:
			pdu_obj = new iscsi_pdu_scsi_r2t(*ses);
			break;
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_scsi_taskman:
			pdu_obj = new iscsi_pdu_taskman_request(*ses);
			break;
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_scsi_data_out:
			pdu_obj = new iscsi_pdu_scsi_data_out(*ses);
			break;
		default:
			DOLOG(logging::ll_error, "server::receive_pdu", cc->get_endpoint_name(), "opcode %02xh not implemented", bhs.get_opcode());
			pdu_obj = new iscsi_pdu_bhs(*ses);
			pdu_error = true;
			break;
	}

	if (pdu_obj) {
		bool ok = true;

		if (pdu_obj->set(pdu, sizeof pdu) == false) {
			ok = false;
			DOLOG(logging::ll_debug, "server::receive_pdu", cc->get_endpoint_name(), "initialize PDU: validation failed");
		}

		if (bhs.get_opcode() == iscsi_pdu_bhs::iscsi_bhs_opcode::o_login_req) {
			is->iscsiTgtLoginAccepts++;
			auto initiator = reinterpret_cast<iscsi_pdu_login_request *>(pdu_obj)->get_initiator();
			if (initiator.has_value()) {
#ifdef ESP32
				Serial.print(F("Initiator: "));
				Serial.println(initiator.value().c_str());
#else
				DOLOG(logging::ll_debug, "server::receive_pdu", cc->get_endpoint_name(), "initiator: %s", initiator.value().c_str());
#endif
			}
		}

		if (bhs.get_opcode() == iscsi_pdu_bhs::iscsi_bhs_opcode::o_logout_req)
			is->iscsiTgtLogoutNormals++;

		size_t ahs_len = pdu_obj->get_ahs_length();
		if (ahs_len) {
			DOLOG(logging::ll_debug, "server::receive_pdu", cc->get_endpoint_name(), "read %zu ahs bytes", ahs_len);

			uint8_t *ahs_temp = new uint8_t[ahs_len]();
			if (cc->recv(ahs_temp, ahs_len) == false) {
				ok = false;
				DOLOG(logging::ll_info, "server::receive_pdu", cc->get_endpoint_name(), "AHS receive error");
			}
			else {
				pdu_obj->set_ahs_segment({ ahs_temp, ahs_len });
			}
			delete [] ahs_temp;

			(*ses)->add_bytes_rx(ahs_len);
			is->iscsiSsnRxDataOctets += ahs_len;
		}

		size_t data_length = pdu_obj->get_data_length();
		if (data_length) {
			size_t padded_data_length = (data_length + 3) & ~3;

			DOLOG(logging::ll_debug, "server::receive_pdu", cc->get_endpoint_name(), "read %zu data bytes (%zu with padding)", data_length, padded_data_length);

			uint8_t *data_temp = new uint8_t[padded_data_length]();
			if (cc->recv(data_temp, padded_data_length) == false) {
				ok = false;
				DOLOG(logging::ll_info, "server::receive_pdu", cc->get_endpoint_name(), "data receive error");
			}
			else {
				pdu_obj->set_data({ data_temp, data_length });
			}
			delete [] data_temp;

			(*ses)->add_bytes_rx(padded_data_length);
			is->iscsiSsnRxDataOctets += padded_data_length;
		}
		
		if (!ok) {
			DOLOG(logging::ll_debug, "server::receive_pdu", cc->get_endpoint_name(), "cannot return PDU");
			delete pdu_obj;
			pdu_obj = nullptr;
		}
	}

	return { pdu_obj, pdu_error, tx_start };
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
			DOLOG(logging::ll_debug, "server::push_response", cc->get_endpoint_name(), "DATA-OUT PDU references unknown TTT (%08x)", TTT);
			return false;
		}
		else if (data.has_value() && data.value().second > 0) {
			auto block_size = s->get_block_size();
			DOLOG(logging::ll_debug, "server::push_response", cc->get_endpoint_name(), "writing %zu bytes to offset LBA %zu + offset %u => %zu (in bytes)", data.value().second, session->buffer_lba, offset, session->buffer_lba * block_size + offset);

			assert((offset % block_size) == 0);
			assert((data.value().second % block_size) == 0);

			auto rc = s->write(session->buffer_lba + offset / block_size, data.value().second / block_size, data.value().first);
			if (rc != scsi::rw_ok) {
				DOLOG(logging::ll_info, "server::push_response", cc->get_endpoint_name(), "DATA-OUT problem writing to backend (%d)", rc);
				return false;
			}

			if (session->fua) {
				if (s->sync() == false) {
					DOLOG(logging::ll_error, "server::push_response", cc->get_endpoint_name(), "DATA-OUT problem syncing data to backend");
					return false;
				}
			}

			session->bytes_done += data.value().second;
			session->bytes_left -= data.value().second;
		}
		else if (!F) {
			DOLOG(logging::ll_warning, "server::push_response", cc->get_endpoint_name(), "DATA-OUT PDU has no data?");
			return false;
		}

		// create response
		if (F) {
			DOLOG(logging::ll_debug, "server::push_response", cc->get_endpoint_name(), "end of batch");

			iscsi_pdu_scsi_cmd response(ses);
			if (response.set(session->PDU_initiator.data, session->PDU_initiator.n) == false) {
				DOLOG(logging::ll_error, "server::push_response", cc->get_endpoint_name(), "response.set failed");
				return false;
			}
			response_set = response.get_response(sd, session->bytes_left);

			if (session->bytes_left == 0) {
				DOLOG(logging::ll_debug, "server::push_response", cc->get_endpoint_name(), "end of task");

				ses->remove_r2t_session(TTT);
			}
			else {
				DOLOG(logging::ll_debug, "server::push_response", cc->get_endpoint_name(), "ask for more (%u bytes left)", session->bytes_left);
				// send 0x31 for range
				iscsi_pdu_scsi_cmd temp(ses);
				temp.set(session->PDU_initiator.data, session->PDU_initiator.n);

				auto *response = new iscsi_pdu_scsi_r2t(ses) /* 0x31 */;
				if (response->set(temp, TTT, session->bytes_done, session->bytes_left) == false) {
					DOLOG(logging::ll_error, "server::push_response", cc->get_endpoint_name(), "response->set failed");
					delete response;
					return false;
				}

				response_set.value().responses.push_back(response);
			}
		}
	}
	else {
		response_set = pdu->get_response(sd);
	}

	if (response_set.has_value() == false) {
		DOLOG(logging::ll_debug, "server::push_response", cc->get_endpoint_name(), "no response from PDU");
		return true;
	}

	for(auto & pdu_out: response_set.value().responses) {
		for(auto & blobs: pdu_out->get()) {
			if (blobs.data == nullptr) {
				ok = false;
				DOLOG(logging::ll_error, "server::push_response", cc->get_endpoint_name(), "PDU did not emit data bundle");
			}

			assert((blobs.n & 3) == 0);

			if (ok) {
				ok = cc->send(blobs.data, blobs.n);
				if (!ok)
					DOLOG(logging::ll_info, "server::push_response", cc->get_endpoint_name(), "sending PDU to peer failed (%s)", strerror(errno));
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

		DOLOG(logging::ll_debug, "server::push_response", cc->get_endpoint_name(), "stream %u sectors, LBA: %zu", stream_parameters.n_sectors, size_t(stream_parameters.lba));

		iscsi_pdu_scsi_cmd reply_to(ses);
		auto temp = pdu->get_raw();
		reply_to.set(temp.data, temp.n);
		delete [] temp.data;

	        uint64_t use_pdu_data_size = uint64_t(stream_parameters.n_sectors) * s->get_block_size();
		if (use_pdu_data_size > reply_to.get_ExpDatLen()) {
			DOLOG(logging::ll_debug, "server::push_response", cc->get_endpoint_name(), "requested less (%u) than wat is available (%" PRIu64 ")", reply_to.get_ExpDatLen(), use_pdu_data_size);
			use_pdu_data_size = reply_to.get_ExpDatLen();
		}

		size_t buffer_n     = 0;
		auto   ack_interval = ses->get_ack_interval();
#ifdef ESP32
		size_t heap_free = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
		if (heap_free >= 5120) {
			size_t heap_allowed = (heap_free - 2048) / 4;

			if (ack_interval.has_value())
				buffer_n = std::min(heap_allowed, size_t(ack_interval.value()));
			else
				buffer_n = heap_allowed;
		}
#elif defined(RP2040W)
		if (ack_interval.has_value())
			buffer_n = std::min(uint32_t(4096), ack_interval.value());
		else
			buffer_n = 4096;
#elif defined(TEENSY4_1)
		if (ack_interval.has_value())
			buffer_n = std::min(uint32_t(16384), ack_interval.value());
		else
			buffer_n = 16384;
#else
		if (ack_interval.has_value())
			buffer_n = std::max(uint32_t(s->get_block_size()), ack_interval.value());
		else
			buffer_n = std::max(uint32_t(s->get_block_size()), uint32_t(65536));  // 64 kB, arbitrarily chosen
#endif
		if (buffer_n < s->get_block_size())
			buffer_n = s->get_block_size();

		uint32_t block_group_size = buffer_n / s->get_block_size();
		uint32_t offset           = 0;
		bool     low_ram          = false;

		for(uint32_t block_nr = 0; block_nr < stream_parameters.n_sectors;) {
			uint32_t n_left = low_ram ? 1 : std::min(block_group_size, stream_parameters.n_sectors - block_nr);
			buffer_n = n_left * s->get_block_size();

			if (offset < use_pdu_data_size)
				buffer_n = std::min(buffer_n, size_t(use_pdu_data_size - offset));
			else
				buffer_n = 0;

			auto [ out, data_pointer ] = iscsi_pdu_scsi_data_in::gen_data_in_pdu(ses, reply_to, use_pdu_data_size, offset, buffer_n);

			uint64_t cur_block_nr = block_nr + stream_parameters.lba;
			DOLOG(logging::ll_debug, "server::push_response", cc->get_endpoint_name(), "reading %u block(s) nr %zu from backend", n_left, size_t(cur_block_nr));
			if (buffer_n > 0) {
				auto rc = s->read(cur_block_nr, n_left, data_pointer);

				if (rc != scsi::rw_ok) {
					delete [] out.data;
					DOLOG(logging::ll_error, "server::push_response", cc->get_endpoint_name(), "reading %u block(s) %zu from backend failed (%d)", n_left, size_t(cur_block_nr), rc);
					ok = false;
					break;
				}
			}

			if (out.n == 0) {  // gen_data_in_pdu could not allocate memory
				DOLOG(logging::ll_warning, "server::push_response", cc->get_endpoint_name(), "low on memory: %zu bytes failed", buffer_n);
				low_ram  = true;
				buffer_n = s->get_block_size();
			}
			else {
				bool rc = cc->send(out.data, out.n);
				delete [] out.data;
				if (rc == false) {
					DOLOG(logging::ll_info, "server::push_response", cc->get_endpoint_name(), "problem sending block %zu to initiator", size_t(cur_block_nr));
					ok = false;
					break;
				}

				ses->add_bytes_tx(out.n);
				offset   += buffer_n;
				block_nr += n_left;
				is->iscsiSsnTxDataOctets += out.n;
			}

			if (buffer_n == 0)
				break;
		}
	}

	DOLOG(logging::ll_debug, "server::push_response", "-", "---");

	return ok;
}

void server::handler()
{
	std::vector<std::pair<std::thread *, std::atomic_bool *> > threads;

	while(!stop) {
		com_client *cc = c->accept();
		if (cc == nullptr) {
			DOLOG(logging::ll_error, "server::handler", "-", "accept() failed: %s", strerror(errno));
			continue;
		}

#if !defined(TEENSY4_1)
		for(size_t i=0; i<threads.size();) {
			if (*threads.at(i).second) {
				DOLOG(logging::ll_debug, "server::handler", "-", "thread cleaned up");
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
			Serial.printf("new session with %s\r", endpoint.c_str());
#else
			DOLOG(logging::ll_info, "server::handler", "-", "new session with %s", endpoint.c_str());
#endif
			auto     prev_output = get_millis();
			uint32_t pdu_count   = 0;
			auto     start       = prev_output;
			unsigned long busy   = 0;
			const long interval  = 5000;

			session *ses = nullptr;
			bool     ok  = true;

			do {
				auto incoming = receive_pdu(cc, &ses);
				iscsi_pdu_bhs *pdu = std::get<0>(incoming);
				if (!pdu) {
					DOLOG(logging::ll_info, "server::handler", endpoint, "no PDU received, aborting socket connection");
					is->iscsiInstSsnFailures++;
					break;
				}

				is->iscsiSsnCmdPDUs++;

				if (std::get<1>(incoming)) {  // something wrong with the received PDU?
					DOLOG(logging::ll_debug, "server::handler", endpoint, "invalid PDU received");

					is->iscsiInstSsnFormatErrors++;

					std::optional<blob_t> reject = generate_reject_pdu(*pdu);
					if (reject.has_value() == false) {
						DOLOG(logging::ll_error, "server::handler", endpoint, "cannot generate reject PDU");
						continue;
					}

					bool rc = cc->send(reject.value().data, reject.value().n);
					delete [] reject.value().data;
					if (rc == false) {
						DOLOG(logging::ll_error, "server::handler", endpoint, "cannot transmit reject PDU");
						break;
					}
					is->iscsiSsnTxDataOctets += reject.value().n;

					DOLOG(logging::ll_debug, "server::handler", endpoint, "transmitted reject PDU");
				}
				else {
					ok = push_response(cc, ses, pdu, s);
					if (!ok)
						is->iscsiInstSsnFailures++;
				}

				delete pdu;

				pdu_count++;

				auto tx_start = std::get<2>(incoming);
				auto tx_end   = get_micros();
				busy += tx_end - tx_start;

				auto now  = get_millis();
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
					DOLOG(logging::ll_info, "server::handler", endpoint, "PDU/s: %.2f, send: %.2f kB/s, recv: %.2f kB/s, written: %.2f kB/s, read: %.2f kB/s, syncs: %.2f/s, unmaps: %.2f/s, load: %.2f%%", pdu_count / dtook, ses->get_bytes_tx() / dkB, ses->get_bytes_rx() / dkB, bytes_written / dkB, bytes_read / dkB, n_syncs / dtook, n_trims / dtook, busy * 0.1 / took);
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
			DOLOG(logging::ll_debug, "server::handler", endpoint, "session finished");
#endif

			s->sync();
			if (s->locking_status() == scsi::l_locked) {
				DOLOG(logging::ll_debug, "server::handler", endpoint, "unlocking device");
				s->unlock_device();
			}

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
