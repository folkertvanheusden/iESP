#include <cstdint>
#include <cstdio>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "iscsi-pdu.h"
#include "server.h"
#include "utils.h"


server::server()
{
}

server::~server()
{
	close(listen_fd);
}

bool server::begin()
{
	// setup listening socket for viewers
	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd == -1)
		return false;

        int reuse_addr = 1;
        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse_addr, sizeof(reuse_addr)) == -1)
		return false;

#ifdef linux
	int q_size = SOMAXCONN;
	if (setsockopt(listen_fd, SOL_TCP, TCP_FASTOPEN, &q_size, sizeof q_size))
		return false;
#endif

        sockaddr_in server_addr { };
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(3260);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(listen_fd, reinterpret_cast<sockaddr *>(&server_addr), sizeof server_addr) == -1)
		return false;

        if (listen(listen_fd, q_size) == -1)
                return false;

	return true;
}

iscsi_pdu_bhs *server::receive_pdu(const int fd, session **const s)
{
	if (*s == nullptr)
		*s = new session();

	uint8_t pdu[48] { 0 };
	if (READ(fd, pdu, sizeof pdu) == -1)
		return nullptr;

	iscsi_pdu_bhs bhs;
	if (bhs.set(*s, pdu, sizeof pdu) == false)
		return nullptr;

	printf("opcode: %02x / %s\n", bhs.get_opcode(), pdu_opcode_to_string(bhs.get_opcode()).c_str());

	iscsi_pdu_bhs *pdu_obj = nullptr;

	switch(bhs.get_opcode()) {
		case iscsi_pdu_bhs::iscsi_bhs_opcode::o_login_req:
			pdu_obj = new iscsi_pdu_login_request();
			break;
	}

	if (pdu_obj) {
		bool ok = true;

		if (pdu_obj->set(*s, pdu, sizeof pdu) == false)
			ok = false;

		size_t ahs_len = pdu_obj->get_ahs_length();
		if (ahs_len) {
			uint8_t *ahs_temp = new uint8_t[ahs_len];
			if (READ(fd, ahs_temp, ahs_len) == -1)
				ok = false;
			else
				pdu_obj->set_ahs_segment({ ahs_temp, ahs_len });
			delete ahs_temp;
		}

		size_t data_length = pdu_obj->get_data_length();
		if (data_length) {
			uint8_t *data_temp = new uint8_t[data_length];
			if (READ(fd, data_temp, data_length) == -1)
				ok = false;
			else
				pdu_obj->set_data({ data_temp, data_length });
			delete data_temp;
		}
		
		if (!ok) {
			delete pdu_obj;
			pdu_obj = nullptr;
		}
	}

	return pdu_obj;
}

void server::handler()
{
	scsi scsi_dev;

	for(;;) {
		int fd = accept(listen_fd, nullptr, nullptr);
		if (fd == -1)
			continue;

		session *s  = nullptr;
		bool     ok = true;

		do {
			iscsi_pdu_bhs *pdu = receive_pdu(fd, &s);
			if (!pdu)
				break;

			/*
			uint8_t *data        = nullptr;
			size_t   data_length = bhs.get_data_length();
			printf("DATA size: %zu (%x)\n", data_length, unsigned(data_length));
			if (data_length) {
				bool ok = true;

				data = new uint8_t[data_length];
				if (READ(fd, data, data_length) == -1)
					ok = false;

				size_t padding = ((data_length + 3) & ~3) - data_length;
				if (ok && padding) {
					uint8_t padding_temp[4];
					if (READ(fd, padding_temp, padding) == -1)
						ok = false;
				}

				if (!ok) {
					delete [] ahs;
					delete [] data;
					break;
				}
			}

			delete [] data;
			*/
		}
		while(ok);

		close(fd);

		delete s;
	}
}
