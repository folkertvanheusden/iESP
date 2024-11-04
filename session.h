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
	com_client *const connected_to  { nullptr };  // e.g. for retrieving the local address
	const std::string target_name;
	uint32_t          data_sn_itt   { 0       };  // itt == initiator transfer tag
	uint32_t          data_sn       { 0       };
	uint32_t          block_size    { 0       };

	struct {
		uint64_t  bytes_rx      { 0       };
		uint64_t  bytes_tx      { 0       };
	} statistics;

	uint32_t          max_seg_len   { MAX_DATA_SEGMENT_SIZE };

	const bool        allow_digest  { false   };
	bool              header_digest { false   };
	bool              data_digest   { false   };

	std::optional<uint32_t> ack_interval;

	std::map<uint32_t, r2t_session *> r2t_sessions; // r2t sessions

public:
	session(com_client *const connected_to, const std::string & target_name, const bool allow_digest);
	virtual ~session();

	std::string get_target_name  () const { return target_name;                       }
	std::string get_local_address() const { return connected_to->get_local_address(); }
	std::string get_endpoint_name() const { return connected_to->get_endpoint_name(); }

	void     set_header_digest(const bool v) { header_digest = v; }
	void     set_data_digest  (const bool v) { data_digest   = v; }
	bool     get_header_digest() const { return header_digest & allow_digest; }
	bool     get_data_digest  () const { return data_digest & allow_digest;   }

	void     set_max_seg_len(const uint32_t v) { max_seg_len = v; }
	uint32_t get_max_seg_len() const { return max_seg_len; }

	void     add_bytes_rx(const uint64_t n) { statistics.bytes_rx += n; }
	uint64_t get_bytes_rx() const { return statistics.bytes_rx; }
	void     reset_bytes_rx() { statistics.bytes_rx = 0; }
	void     add_bytes_tx(const uint64_t n) { statistics.bytes_tx += n; }
	uint64_t get_bytes_tx() const { return statistics.bytes_tx; }
	void     reset_bytes_tx() { statistics.bytes_tx = 0; }

	uint32_t get_inc_datasn(const uint32_t itt);

	void     set_block_size(const uint32_t block_size_in) { block_size = block_size_in; }
	uint32_t get_block_size() const { return block_size; }

	void     set_ack_interval(const uint32_t bytes) { ack_interval = bytes; }
	std::optional<uint32_t> get_ack_interval()      { return ack_interval;  }

	void     init_r2t_session(const r2t_session & rs, const bool fua, iscsi_pdu_scsi_cmd *const pdu, const uint32_t transfer_tag);
	r2t_session *get_r2t_sesion(const uint32_t ttt);
	void     remove_r2t_session(const uint32_t ttt);
};
