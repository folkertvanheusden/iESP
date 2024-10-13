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
#include <thread>
#include <unistd.h>
#if !defined(RP2040W) && !defined(TEENSY4_1) && !defined(__MINGW32__)
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

server::server(scsi *const s, com *const c, iscsi_stats_t *is, const std::string & target_name, const bool digest_chk):
	s(s),
	c(c),
	is(is),
	target_name(target_name),
	digest_chk(digest_chk)
{
}

server::~server()
{
#if !defined(ARDUINO) && !defined(NDEBUG)
	DOLOG(logging::ll_info, "~server()", "-", "iSCSI opcode usage counts:");
	for(int opcode=0; opcode<64; opcode++) {
		auto     descr   = pdu_opcode_to_string(iscsi_pdu_bhs::iscsi_bhs_opcode(opcode));
		uint64_t u_count = cmd_use_count[opcode];

		if (descr.has_value() || u_count > 0)
			DOLOG(logging::ll_info, "~server()", "-", "  %02x: %" PRIu64 " (%s)", opcode, u_count, descr.has_value() ? descr.value().c_str() : "?");
	}
#endif
}

std::tuple<iscsi_pdu_bhs *, iscsi_fail_reason, uint64_t> server::receive_pdu(com_client *const cc, session **const ses)
{
	if (*ses == nullptr) {
		*ses = new session(cc, target_name, digest_chk);
		(*ses)->set_block_size(s->get_block_size());
	}

	uint8_t pdu[48] { };
	if (cc->recv(pdu, sizeof pdu) == false) {
		DOLOG(logging::ll_info, "server::receive_pdu", cc->get_endpoint_name(), "PDU receive error");
		return { nullptr, IFR_CONNECTION, 0 };
	}

	uint64_t tx_start = get_micros();

	(*ses)->add_bytes_rx(sizeof pdu);
	is->iscsiSsnRxDataOctets += sizeof pdu;

	iscsi_pdu_bhs bhs(*ses);
	if (bhs.set(pdu, sizeof pdu) == false) {
		DOLOG(logging::ll_debug, "server::receive_pdu", cc->get_endpoint_name(), "BHS validation error");
		return { nullptr, IFR_INVALID_FIELD, tx_start };
	}

#if defined(ESP32) || defined(RP2040W)
//	slow!
//	Serial.print(millis());
//	Serial.print(' ');
//	Serial.println(pdu_opcode_to_string(bhs.get_opcode()).c_str());
#else
	auto  descr = pdu_opcode_to_string(iscsi_pdu_bhs::iscsi_bhs_opcode(bhs.get_opcode()));
	DOLOG(logging::ll_debug, "server::receive_pdu", cc->get_endpoint_name(), "opcode: %02xh / %s", bhs.get_opcode(), descr.has_value() ? descr.value().c_str() : "?");
#endif

	iscsi_pdu_bhs    *pdu_obj    = nullptr;
	iscsi_fail_reason pdu_error  = IFR_OK;
	bool              has_digest = true;
	auto              opcode     = bhs.get_opcode();
#if !defined(ARDUINO) && !defined(NDEBUG)
	cmd_use_count[opcode]++;
#endif

	switch(opcode) {
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_login_req:
			pdu_obj = new iscsi_pdu_login_request(*ses);
			has_digest = false;
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
			pdu_error = IFR_INVALID_COMMAND;
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

		auto incoming_crc32c = crc32_0x11EDC6F41(pdu, sizeof pdu, { });

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
			incoming_crc32c = crc32_0x11EDC6F41(ahs_temp, ahs_len, incoming_crc32c.second);
			delete [] ahs_temp;

			(*ses)->add_bytes_rx(ahs_len);
			is->iscsiSsnRxDataOctets += ahs_len;
		}

		if ((*ses)->get_header_digest() && has_digest) {
			uint32_t remote_header_digest = 0;

			if (cc->recv(reinterpret_cast<uint8_t *>(&remote_header_digest), sizeof remote_header_digest) == false) {
				ok = false;
				DOLOG(logging::ll_info, "server::receive_pdu", cc->get_endpoint_name(), "header digest receive error");
			}
			else {
				// verify digest
				if (remote_header_digest != incoming_crc32c.first && digest_chk) {
					ok        = false;
					pdu_error = IFR_DIGEST;
					DOLOG(logging::ll_info, "server::receive_pdu", cc->get_endpoint_name(), "header digest mismatch: received=%08x, calculated=%08x", remote_header_digest, incoming_crc32c.first);
				}

				is->iscsiSsnRxDataOctets += sizeof remote_header_digest;
			}
		}

		size_t data_length = pdu_obj->get_data_length();
		if (data_length > MAX_DATA_SEGMENT_SIZE) {
			DOLOG(logging::ll_debug, "server::receive_pdu", cc->get_endpoint_name(), "initiator is pushing too many data (%zu bytes, max is %u)", data_length, MAX_DATA_SEGMENT_SIZE);
			ok        = false;
			pdu_error = IFR_INVALID_FIELD;
		}
		else if (data_length) {
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
			std::pair<uint32_t, uint32_t> incoming_crc32c { };
			if (digest_chk)
				incoming_crc32c = crc32_0x11EDC6F41(data_temp, padded_data_length, { });
			delete [] data_temp;

			(*ses)->add_bytes_rx(padded_data_length);
			is->iscsiSsnRxDataOctets += padded_data_length;

			if ((*ses)->get_data_digest() && has_digest) {
				uint32_t remote_data_digest = 0;

				if (cc->recv(reinterpret_cast<uint8_t *>(&remote_data_digest), sizeof remote_data_digest) == false) {
					ok = false;
					DOLOG(logging::ll_info, "server::receive_pdu", cc->get_endpoint_name(), "data digest receive error");
				}
				else {
					// verify digest
					if (remote_data_digest != incoming_crc32c.first && digest_chk) {
						ok        = false;
						pdu_error = IFR_DIGEST;
						DOLOG(logging::ll_info, "server::receive_pdu", cc->get_endpoint_name(), "data digest mismatch: received=%08x, calculated=%08x", remote_data_digest, incoming_crc32c.first);
					}

					is->iscsiSsnRxDataOctets += sizeof remote_data_digest;
				}
			}
		}
		
		if (!ok) {
			DOLOG(logging::ll_debug, "server::receive_pdu", cc->get_endpoint_name(), "cannot return PDU");
			delete pdu_obj;
			pdu_obj = nullptr;
		}
	}

	return { pdu_obj, pdu_error, tx_start };
}

iscsi_fail_reason server::push_response(com_client *const cc, session *const ses, iscsi_pdu_bhs *const pdu)
{
	iscsi_fail_reason ifr = IFR_OK;

	std::optional<iscsi_response_set> response_set;

	if (pdu->get_opcode() == iscsi_pdu_bhs::iscsi_bhs_opcode::o_scsi_data_out) {
		auto     pdu_data_out = reinterpret_cast<iscsi_pdu_scsi_data_out *>(pdu);
		uint32_t offset       = pdu_data_out->get_BufferOffset();
		auto     data         = pdu_data_out->get_data();
		uint32_t transfer_tag = pdu_data_out->get_TTT();
		bool     F            = pdu_data_out->get_F();
		auto     session      = ses->get_r2t_sesion(transfer_tag);

		if (transfer_tag == 0xffffffff) {  // unsollicited data
			transfer_tag = pdu_data_out->get_Itasktag();
			session      = ses->get_r2t_sesion(transfer_tag);
		}

		DOLOG(logging::ll_debug, "server::push_response", cc->get_endpoint_name(), "TT: %08x, F: %d, session: %d, offset: %u, has data: %u", transfer_tag, F, session != nullptr, offset, data.has_value() ? data.value().second : 0);

		if (session == nullptr) {
			DOLOG(logging::ll_debug, "server::push_response", cc->get_endpoint_name(), "DATA-OUT PDU references unknown TTT (%08x)", transfer_tag);
			return IFR_INVALID_FIELD;
		}
		else if (data.has_value() && data.value().second > 0) {
			auto block_size = s->get_block_size();
			DOLOG(logging::ll_debug, "server::push_response", cc->get_endpoint_name(), "writing %zu bytes to offset LBA %zu + offset %u => %zu (in bytes)", data.value().second, session->buffer_lba, offset, session->buffer_lba * block_size + offset);

			 if (offset % block_size) {
				DOLOG(logging::ll_info, "server::push_response", cc->get_endpoint_name(), "offset is not multiple of block size");
				return IFR_INVALID_FIELD;
			 }

			if (data.value().second % block_size) {
				DOLOG(logging::ll_info, "server::push_response", cc->get_endpoint_name(), "data size is not multiple of block size");
				return IFR_INVALID_FIELD;
			}

			scsi::scsi_rw_result rc = scsi::scsi_rw_result::rw_ok;

			if (session->is_write_same) {
				uint32_t n_blocks = session->bytes_left / block_size;

				if (session->write_same_is_unmap) {  // makes no sense to do this in R2T?
					rc = s->trim(session->buffer_lba, session->bytes_left / block_size);
				}
				else {
					uint64_t lba = session->buffer_lba;

					for(uint32_t i=0; i<n_blocks; i++) {
						rc = s->write(lba, block_size, data.value().first);
						if (rc != scsi::rw_ok)
							break;
					}
				}
			}
			else {
				rc = s->write(session->buffer_lba + offset / block_size, data.value().second / block_size, data.value().first);
			}

			if (rc != scsi::rw_ok) {
				DOLOG(logging::ll_info, "server::push_response", cc->get_endpoint_name(), "DATA-OUT problem writing to backend: %d", rc);
				return IFR_IO_ERROR;
			}

			session->bytes_done += data.value().second;
			session->bytes_left -= data.value().second;
		}
		else if (!F) {
			DOLOG(logging::ll_warning, "server::push_response", cc->get_endpoint_name(), "DATA-OUT PDU has no data?");
			return IFR_INVALID_FIELD;
		}

		// create response
		if (F) {
			DOLOG(logging::ll_debug, "server::push_response", cc->get_endpoint_name(), "end of batch");

			iscsi_pdu_scsi_cmd response(ses);
			if (response.set(session->PDU_initiator.data, session->PDU_initiator.n) == false) {
				DOLOG(logging::ll_error, "server::push_response", cc->get_endpoint_name(), "response.set failed");
				return IFR_MISC;
			}
			response_set = response.get_response(s, session->bytes_left);

			if (session->bytes_left == 0) {
				DOLOG(logging::ll_debug, "server::push_response", cc->get_endpoint_name(), "end of task");

				ses->remove_r2t_session(transfer_tag);
			}
			else {
				DOLOG(logging::ll_debug, "server::push_response", cc->get_endpoint_name(), "ask for more (%u bytes left)", session->bytes_left);
				// send 0x31 for range
				iscsi_pdu_scsi_cmd temp(ses);
				temp.set(session->PDU_initiator.data, session->PDU_initiator.n);

				auto *response = new iscsi_pdu_scsi_r2t(ses) /* 0x31 */;
				if (response->set(temp, transfer_tag, session->bytes_done, session->bytes_left) == false) {
					DOLOG(logging::ll_error, "server::push_response", cc->get_endpoint_name(), "response->set failed");
					delete response;
					return IFR_MISC;
				}

				response_set.value().responses.push_back(response);
			}
		}
	}
	else {
		response_set = pdu->get_response(s);
	}

	if (response_set.has_value() == false) {
		DOLOG(logging::ll_debug, "server::push_response", cc->get_endpoint_name(), "no response from PDU");
		return IFR_OK;
	}

	for(auto & pdu_out: response_set.value().responses) {
		for(auto & blobs: pdu_out->get()) {
			if (blobs.data == nullptr) {
				ifr = IFR_INVALID_FIELD;
				DOLOG(logging::ll_error, "server::push_response", cc->get_endpoint_name(), "PDU did not emit data bundle");
			}

			assert((blobs.n & 3) == 0);

			if (ifr == IFR_OK) {
				bool ok = cc->send(blobs.data, blobs.n);
				if (!ok) {
					ifr = IFR_CONNECTION;
					DOLOG(logging::ll_info, "server::push_response", cc->get_endpoint_name(), "sending PDU to peer failed (%s)", strerror(errno));
				}

				ses->add_bytes_tx(blobs.n);
				is->iscsiSsnTxDataOctets += blobs.n;
			}

			delete [] blobs.data;
		}

		delete pdu_out;
	}

	// e.g. for READ_xx (as buffering may be RAM-wise too costly (on microcontrollers)) -> DATA-IN
	if (response_set.value().to_stream.has_value()) {
		auto & stream_parameters = response_set.value().to_stream.value();

		iscsi_pdu_scsi_cmd reply_to(ses);
		auto temp = pdu->get_raw();
		reply_to.set(temp.data, temp.n);
		delete [] temp.data;

		DOLOG(logging::ll_debug, "server::push_response", cc->get_endpoint_name(), "SCSI: stream %u sectors starting at LBA %" PRIu64 ", iSCSI: %u", stream_parameters.n_sectors, stream_parameters.lba, reply_to.get_ExpDatLen());

		uint64_t buffer_n    = MAX_DATA_SEGMENT_SIZE;

		uint32_t scsi_has    = stream_parameters.n_sectors * s->get_block_size();
		uint32_t iscsi_wants = reply_to.get_ExpDatLen();

		uint64_t current_lba = stream_parameters.lba;
		uint32_t offset      = 0;
		uint32_t offset_end  = std::min(scsi_has, iscsi_wants);

		if (offset_end == 0) {
                        auto *temp = new iscsi_pdu_scsi_response(ses) /* 0x21 */;

			std::optional<std::pair<residual, uint32_t> > residual_state;

                        if (scsi_has < iscsi_wants)
                                residual_state = { iSR_UNDERFLOW, iscsi_wants - scsi_has };
                        else if (scsi_has > iscsi_wants)
                                residual_state = { iSR_OVERFLOW, scsi_has - iscsi_wants };

                        if (temp->set(reply_to, { }, residual_state, { }) == false) {
                                ifr = IFR_MISC;
                                DOLOG(logging::ll_info, "server::push_response", cc->get_endpoint_name(), "iscsi_pdu_scsi_response::set returned error");
                        }

			auto out = temp->get()[0];

			bool rc_tx = cc->send(out.data, out.n);
			delete [] out.data;
			if (rc_tx == false) {
				DOLOG(logging::ll_info, "server::push_response", cc->get_endpoint_name(), "problem sending %zu bytes", out.n);
				ifr = IFR_CONNECTION;
			}
			else {
				ses->add_bytes_tx(out.n);
				is->iscsiSsnTxDataOctets += out.n;
			}

			delete temp;
		}

		while(offset < offset_end) {
			uint64_t bytes_left = offset_end - offset;
			uint32_t current_n  = std::min(uint64_t(ses->get_max_seg_len()), std::min(bytes_left, buffer_n));
			bool     last_block = offset + current_n == offset_end;

			std::optional<std::pair<residual, uint32_t> > has_residual;
			if (last_block) {
				if (scsi_has > iscsi_wants)
					has_residual = { iSR_OVERFLOW, scsi_has - iscsi_wants };
				else if (scsi_has < iscsi_wants)
					has_residual = { iSR_UNDERFLOW, iscsi_wants - scsi_has };
			}

			auto [ out, data_pointer ] = iscsi_pdu_scsi_data_in::gen_data_in_pdu(ses, reply_to, has_residual, offset, current_n, last_block);

			scsi::scsi_rw_result rc = scsi::rw_fail_general;

			if (current_n < s->get_block_size()) {
				uint8_t *temp_buffer = new uint8_t[s->get_block_size()];
				rc = s->read(current_lba, 1, temp_buffer);
				if (rc == scsi::rw_ok)
					memcpy(data_pointer, temp_buffer, current_n);
				delete [] temp_buffer;
			}
			else {
				rc = s->read(current_lba, current_n / s->get_block_size(), data_pointer);
			}

			if (rc != scsi::rw_ok) {
				delete [] out.data;
				DOLOG(logging::ll_error, "server::push_response", cc->get_endpoint_name(), "reading %u bytes failed: %d", current_n, rc);
				ifr = IFR_IO_ERROR;  // TODO push a pdu with a scsi read error
				break;
			}

			if (ses->get_data_digest()) {
				size_t   n_bytes = current_n;
				uint32_t crc32   = crc32_0x11EDC6F41(reinterpret_cast<const uint8_t *>(data_pointer), n_bytes, { }).first;
				memcpy(&data_pointer[n_bytes], &crc32, sizeof crc32);
			}

			bool rc_tx = cc->send(out.data, out.n);
			delete [] out.data;
			if (rc_tx == false) {
				DOLOG(logging::ll_info, "server::push_response", cc->get_endpoint_name(), "problem sending %u bytes of block %" PRIu64 " to initiator", current_lba, current_n);
				ifr = IFR_CONNECTION;
				break;
			}

			offset      += current_n;
			current_lba += 1;
			ses->add_bytes_tx(out.n);
			is->iscsiSsnTxDataOctets += out.n;
		}
	}

	DOLOG(logging::ll_debug, "server::push_response", "-", "---");

	return ifr;
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

#if !defined(TEENSY4_1) && !defined(RP2040W)
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
			Serial.printf("new session with %s\r\n", endpoint.c_str());
#else
			DOLOG(logging::ll_info, "server::handler", "-", "new session with %s", endpoint.c_str());
#endif
			auto          prev_output  = get_millis();
			uint32_t      pdu_count    = 0;
			unsigned long busy         = 0;
			const long    interval     = 5000;
			session      *ses          = nullptr;
			bool          ok           = true;
			int           fail_counter = 0;

			do {
				auto incoming = receive_pdu(cc, &ses);
				iscsi_pdu_bhs *pdu = std::get<0>(incoming);
				if (!pdu) {
					DOLOG(logging::ll_info, "server::handler", endpoint, "no PDU received, aborting socket connection");
					is->iscsiInstSsnFailures++;
					break;
				}

				is->iscsiSsnCmdPDUs++;

				iscsi_fail_reason ifr = std::get<1>(incoming);
				if (ifr == IFR_OK) {
					ifr = push_response(cc, ses, pdu);
					if (ifr != IFR_OK)
						is->iscsiInstSsnFailures++;
				}

				if (ifr != IFR_OK && ifr != IFR_CONNECTION) {  // something wrong with the received PDU?
					DOLOG(logging::ll_debug, "server::handler", endpoint, "invalid PDU");

					std::optional<uint8_t> reason;

					if (ifr == IFR_INVALID_FIELD || ifr == IFR_IO_ERROR || ifr == IFR_MISC) {
						is->iscsiInstSsnFormatErrors++;
						reason = 0x09;
					}
					else if (ifr == IFR_DIGEST) {
						is->iscsiInstSsnDigestErrors++;
						reason = 0x02;
					}
					else if (ifr == IFR_INVALID_COMMAND)
						reason = 0x05;
					else {
						DOLOG(logging::ll_error, "server::handler", endpoint, "internal error, IFR %d not known", ifr);
					}

					std::optional<blob_t> reject = generate_reject_pdu(*pdu, reason);
					if (reject.has_value() == false) {
						DOLOG(logging::ll_error, "server::handler", endpoint, "cannot generate reject PDU");
						continue;
					}

					bool rc = cc->send(reject.value().data, reject.value().n);
					delete [] reject.value().data;
					if (rc == false) {
						DOLOG(logging::ll_error, "server::handler", endpoint, "cannot transmit reject PDU");
						ok = false;
					}
					else {
						is->iscsiSsnTxDataOctets += reject.value().n;
						DOLOG(logging::ll_debug, "server::handler", endpoint, "transmitted reject PDU");
					}
				}

				delete pdu;

				pdu_count++;

				if (ifr == IFR_OK)
					fail_counter = 0;
				else {
					fail_counter++;
					if (fail_counter >= 16) {
						ok = false;
						DOLOG(logging::ll_info, "server::handler", endpoint, "disconnecting because of too many consecutive errors");
					}
				}

				if (ifr == IFR_CONNECTION) {
					DOLOG(logging::ll_debug, "server::handler", endpoint, "disconnected");
					ok = false;
				}

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
#if defined(ESP32)
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

#if !defined(TEENSY4_1) && !defined(RP2040W)
			*flag = true;
		});

		threads.push_back({ th, flag });
#endif

#if defined(ESP32)
		Serial.printf("Heap space: %u\r\n", get_free_heap_space());
#endif
	}

#if !defined(TEENSY4_1) && !defined(RP2040W)
	for(auto &e: threads) {
		if (e.second) {
			e.first->join();
			delete e.first;
			delete e.second;
		}
	}
#endif
}
