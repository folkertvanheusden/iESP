#include <cstdint>
#include <utility>

#include "com.h"
#include "scsi.h"
#include "session.h"


typedef struct
{
#if defined(ARDUINO)
	uint32_t iscsiSsnCmdPDUs;  // 1.3.6.1.2.1.142.1.10.2.1.1
	uint32_t iscsiInstSsnFailures;  // 1.3.6.1.2.1.142.1.1.1.1.10
	uint32_t iscsiInstSsnFormatErrors;  // 1.3.6.1.2.1.142.1.1.2.1.3
#else
	uint64_t iscsiSsnCmdPDUs;  // 1.3.6.1.2.1.142.1.10.2.1.1
	uint64_t iscsiInstSsnFailures;  // 1.3.6.1.2.1.142.1.1.1.1.10
	uint64_t iscsiInstSsnFormatErrors;  // 1.3.6.1.2.1.142.1.1.2.1.3
#endif
	uint64_t iscsiSsnTxDataOctets;  // 1.3.6.1.2.1.142.1.10.2.1.3
	uint64_t iscsiSsnRxDataOctets;  // 1.3.6.1.2.1.142.1.10.2.1.4
} iscsi_stats_t;

class server
{
private:
	scsi          *const s          { nullptr };
	com           *const c          { nullptr };
	iscsi_stats_t *const is         { nullptr };
	uint64_t             bytes_recv { 0       };
	uint64_t             bytes_send { 0       };

	std::pair<iscsi_pdu_bhs *, bool> receive_pdu  (com_client *const cc, session **const s);
	bool                             push_response(com_client *const cc, session *const s, iscsi_pdu_bhs *const pdu, iscsi_response_parameters *const parameters);

	iscsi_response_parameters       *select_parameters(iscsi_pdu_bhs *const pdu, session *const ses, scsi *const sd);

public:
	server(scsi *const s, com *const c, iscsi_stats_t *is);
	virtual ~server();

	bool begin();

	void handler();
};
