#include <cstdint>
#include <utility>

#include "backend.h"
#include "com.h"
#include "scsi.h"
#include "session.h"


class server
{
private:
	backend    *const b          { nullptr };
	com        *const c          { nullptr };
	uint64_t          bytes_recv { 0       };
	uint64_t          bytes_send { 0       };

	std::pair<iscsi_pdu_bhs *, bool> receive_pdu  (com_client *const cc, session **const s);
	bool                             push_response(com_client *const cc, session *const s, iscsi_pdu_bhs *const pdu, iscsi_response_parameters *const parameters);

	iscsi_response_parameters       *select_parameters(iscsi_pdu_bhs *const pdu, session *const ses, scsi *const sd);

public:
	server(backend *const b, com *const c);
	virtual ~server();

	bool begin();

	void handler();
};
