#pragma once
#include <cstdint>
#include <map>
#include <optional>

#include "com.h"
#include "gen.h"
#include "iscsi.h"


#define MAX_N_CONCURRENT_R2T 64

class iscsi_pdu_scsi_cmd;

typedef enum { discovery, select_auth_type, reply_with_chap_r, verify_password, auth_success, auth_failed, operational } handshake_state_t;

class session
{
private:
	com_client *const connected_to  { nullptr };  // e.g. for retrieving the local address
	const std::string target_name;
	uint32_t          data_sn_itt   { 0       };  // itt == initiator transfer tag
	uint32_t          data_sn       { 0       };
	uint32_t          block_size    { 0       };

	struct {
		uint64_t   bytes_rx     { 0       };
		uint64_t   bytes_tx     { 0       };
		unsigned   error_count  { 0       };
		io_stats_t is;
	} statistics;

	uint32_t          max_seg_len   { MAX_DATA_SEGMENT_SIZE };

	const bool        allow_digest  { false   };
	bool              header_digest { false   };
	bool              data_digest   { false   };

	std::optional<uint32_t> ack_interval;

	std::map<uint32_t, r2t_session *> r2t_sessions; // r2t sessions

	handshake_state_t          handshake_state { select_auth_type };
	std::optional<uint8_t>     chap_I;
	std::optional<std::string> chap_C;

public:
	session(com_client *const connected_to, const std::string & target_name, const bool allow_digest);
	virtual ~session();

	std::string get_target_name  () const { return target_name;                       }
	std::string get_local_address() const { return connected_to->get_local_address(); }
	std::string get_endpoint_name() const { return connected_to->get_endpoint_name(); }

	handshake_state_t get_handshake_state() const                       { return handshake_state;      }
	void         set_handshake_state(const handshake_state_t new_state) { handshake_state = new_state; }
	uint8_t      get_chap_I();
	std::string  get_chap_C();

	void     set_header_digest(const bool v) { header_digest = v; }
	void     set_data_digest  (const bool v) { data_digest   = v; }
	bool     get_header_digest() const { return header_digest & allow_digest; }
	bool     get_data_digest  () const { return data_digest & allow_digest;   }

	void     set_max_seg_len(const uint32_t v) { max_seg_len = v; }
	uint32_t get_max_seg_len() const { return max_seg_len; }

	void     add_bytes_rx(const uint64_t n) { statistics.bytes_rx += n;      }
	uint64_t get_bytes_rx() const           { return statistics.bytes_rx;    }
	void     reset_bytes_rx()               { statistics.bytes_rx = 0;       }
	void     add_bytes_tx(const uint64_t n) { statistics.bytes_tx += n;      }
	uint64_t get_bytes_tx() const           { return statistics.bytes_tx;    }
	void     reset_bytes_tx()               { statistics.bytes_tx = 0;       }
	io_stats_t *get_io_stats()              { return &statistics.is;         }
	void     reset_io_stats()               { statistics.is.reset();         }
	void     inc_error_count()              { statistics.error_count++;      }
	unsigned get_error_count() const        { return statistics.error_count; }

	uint32_t get_inc_datasn(const uint32_t itt);

	void     set_block_size(const uint32_t block_size_in) { block_size = block_size_in; }
	uint32_t get_block_size() const { return block_size; }

	void     set_ack_interval(const uint32_t bytes) { ack_interval = bytes; }
	std::optional<uint32_t> get_ack_interval()      { return ack_interval;  }

	bool     init_r2t_session(const r2t_session & rs, const bool fua, iscsi_pdu_scsi_cmd *const pdu, const uint32_t transfer_tag);
	r2t_session *get_r2t_sesion(const uint32_t ttt);
	void     remove_r2t_session(const uint32_t ttt);
};
