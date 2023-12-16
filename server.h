#include <cstdint>
#include <utility>


#include "scsi.h"
#include "session.h"

class server
{
private:
	int listen_fd { -1 };

	session * handle_login       (const uint8_t pdu[48], std::pair<uint8_t *, size_t> data, const int fd);
	bool      handle_scsi_command(const uint8_t pdu[48], std::pair<uint8_t *, size_t> data, scsi *const sd, const int fd);

public:
	server();
	virtual ~server();

	bool begin();

	void handler();
};
