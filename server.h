#include <cstdint>
#include <utility>


#include "scsi.h"
#include "session.h"

class server
{
private:
	int listen_fd { -1 };

	iscsi_pdu_bhs *receive_pdu  (const int fd, session **const s);
	bool           push_response(const int fd, iscsi_pdu_bhs *const pdu, iscsi_response_parameters *const parameters);

public:
	server();
	virtual ~server();

	bool begin();

	void handler();
};
