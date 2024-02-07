#include <cstdint>
#include <utility>


#include "backend.h"
#include "scsi.h"
#include "session.h"

class server
{
private:
	backend    *const b           { nullptr };
	const std::string listen_ip;
	const int         listen_port { 3260    };
	int               listen_fd   { -1      };
	uint64_t          bytes_recv  { 0       };
	uint64_t          bytes_send  { 0       };

	iscsi_pdu_bhs *receive_pdu  (const int fd, session **const s);
	bool           push_response(const int fd, session *const s, iscsi_pdu_bhs *const pdu, iscsi_response_parameters *const parameters);

	iscsi_response_parameters *select_parameters(iscsi_pdu_bhs *const pdu, session *const ses, scsi *const sd);

public:
	server(backend *const b, const std::string & listen_ip, const int listen_port);
	virtual ~server();

	bool begin();

	void handler();
};
