#pragma once
#include <cstdint>
#include <map>
#include <optional>

#include "com.h"
#include "iscsi.h"


class iscsi_pdu_scsi_cmd;

class session
{
private:
	com_client *const connected_to { nullptr };  // e.g. for retrieving the local address
	uint32_t          data_sn_itt  { 0       };  // itt == initiator transfer tag
	uint32_t          data_sn      { 0       };
	uint32_t          block_size   { 0       };

	uint64_t          bytes_rx     { 0       };
	uint64_t          bytes_tx     { 0       };

	std::optional<uint32_t> ack_interval;

	std::map<uint32_t, r2t_session *> r2t_sessions; // r2t sessions

public:
	session(com_client *const connected_to);
	virtual ~session();

	std::string get_local_address() const { return connected_to->get_local_address(); }

	void     add_bytes_rx(const uint64_t n) { bytes_rx += n; }
	uint64_t get_bytes_rx() const { return bytes_rx; }
	void     reset_bytes_rx() { bytes_rx = 0; }
	void     add_bytes_tx(const uint64_t n) { bytes_tx += n; }
	uint64_t get_bytes_tx() const { return bytes_tx; }
	void     reset_bytes_tx() { bytes_tx = 0; }

	uint32_t get_inc_datasn(const uint32_t itt);

	void     set_block_size(const uint32_t block_size_in) { block_size = block_size_in; }
	uint32_t get_block_size() const { return block_size; }

	void     set_ack_interval(const uint32_t bytes) { ack_interval = bytes; }
	std::optional<uint32_t> get_ack_interval()      { return ack_interval;  }

	uint32_t init_r2t_session(const r2t_session & rs, iscsi_pdu_scsi_cmd *const pdu);
	r2t_session *get_r2t_sesion(const uint32_t ttt);
	void remove_r2t_session(const uint32_t ttt);
};
