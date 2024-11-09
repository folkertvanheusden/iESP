#include <cstdint>
#include <utility>
#if !defined(TEENSY4_1) && !defined(RP2040W)
#include <atomic>
#include <mutex>
#include <thread>
#endif

#include "com.h"
#include "scsi.h"
#include "session.h"


typedef struct
{
	uint64_t iscsiSsnCmdPDUs;           // 1.3.6.1.2.1.142.1.10.2.1.1
	uint64_t iscsiInstSsnFailures;      // 1.3.6.1.2.1.142.1.1.1.1.10
	uint64_t iscsiInstSsnFormatErrors;  // 1.3.6.1.2.1.142.1.1.2.1.3
	uint64_t iscsiInstSsnDigestErrors;  // 1.3.6.1.2.1.142.1.1.2.1.1
	uint64_t iscsiSsnTxDataOctets;      // 1.3.6.1.2.1.142.1.10.2.1.3
	uint64_t iscsiSsnRxDataOctets;      // 1.3.6.1.2.1.142.1.10.2.1.4
	uint64_t iscsiTgtLoginAccepts;      // 1.3.6.1.2.1.142.1.6.2.1.1
	uint64_t iscsiTgtLogoutNormals;     // 1.3.6.1.2.1.142.1.6.3.1.1
} iscsi_stats_t;

class server
{
private:
	scsi          *const s          { nullptr };
	com           *const c          { nullptr };
	iscsi_stats_t *const is         { nullptr };
	const std::string target_name;
	const bool     digest_chk       { false   };
#if !defined(ARDUINO) && !defined(NDEBUG)
	std::atomic_uint64_t cmd_use_count[64] { };
#endif
#if !defined(TEENSY4_1) && !defined(RP2040W)
	std::mutex     threads_lock;
	std::vector<std::pair<std::thread *, std::atomic_bool *> > threads;
#else
	bool           active           { false   };
#endif

	std::tuple<iscsi_pdu_bhs *, iscsi_fail_reason, uint64_t>
		          receive_pdu  (com_client *const cc, session **const s);
	iscsi_fail_reason push_response(com_client *const cc, session *const s, iscsi_pdu_bhs *const pdu);

public:
	server(scsi *const s, com *const c, iscsi_stats_t *is, const std::string & target_name, const bool digest_chk);
	virtual ~server();

	bool begin();
	bool is_active();
	void handler();
};
