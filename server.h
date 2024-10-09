#include <cstdint>
#include <utility>

#include "com.h"
#include "scsi.h"
#include "session.h"


typedef struct
{
	uint32_t iscsiSsnCmdPDUs;           // 1.3.6.1.2.1.142.1.10.2.1.1
	uint32_t iscsiInstSsnFailures;      // 1.3.6.1.2.1.142.1.1.1.1.10
	uint32_t iscsiInstSsnFormatErrors;  // 1.3.6.1.2.1.142.1.1.2.1.3
	uint64_t iscsiSsnTxDataOctets;      // 1.3.6.1.2.1.142.1.10.2.1.3
	uint64_t iscsiSsnRxDataOctets;      // 1.3.6.1.2.1.142.1.10.2.1.4
	uint32_t iscsiTgtLoginAccepts;      // 1.3.6.1.2.1.142.1.6.2.1.1
	uint32_t iscsiTgtLogoutNormals;     // 1.3.6.1.2.1.142.1.6.3.1.1
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

	std::tuple<iscsi_pdu_bhs *, bool, uint64_t>
		receive_pdu  (com_client *const cc, session **const s);
	bool    push_response(com_client *const cc, session *const s, iscsi_pdu_bhs *const pdu, scsi *const sd);

public:
	server(scsi *const s, com *const c, iscsi_stats_t *is, const std::string & target_name, const bool digest_chk);
	virtual ~server();

	bool begin();

	void handler();
};
