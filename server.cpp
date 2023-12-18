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

session * server::handle_login(const uint8_t pdu[48], std::pair<uint8_t *, size_t> data, const int fd)
{
	iscsi_pdu_login_request plr;
	plr.set(pdu, 48);

	for(size_t i=0; i<data.second; i++) {
		if (data.first[i] == 0x00)
			data.first[i] = '\n';
	}

	auto parts = split(std::string(reinterpret_cast<char *>(data.first), data.second), "\n");

	// TODO verify at least the TargetName
	for(auto & p : parts)
		printf("%s\n", p.c_str());

	if (plr.get_NSG() == 0)  // not supporting SecurityNegotiation
		return nullptr;

	session *s = new session(plr.get_ISID(), plr.get_CID(), plr.get_CmdSN());

	iscsi_pdu_login_reply plrep;
	plrep.set(plr);
	auto iscsi_reply = plrep.get();

	if (WRITE(fd, iscsi_reply.first, iscsi_reply.second) == -1) {
		delete s;
		s = nullptr;
	}

	delete [] iscsi_reply.first;

	return s;
}

bool server::handle_scsi_command(const uint8_t pdu[48], std::pair<uint8_t *, size_t> data, scsi *const sd, const int fd)
{
	bool ok = true;

	iscsi_pdu_scsi_command psc;
	psc.set(pdu, 48);

	auto scsi_reply = sd->send(psc.get_CDB(), 16);
	iscsi_pdu_scsi_response pscr;
	pscr.set(psc, scsi_reply);
	delete [] std::get<1>(scsi_reply);

	auto iscsi_reply = pscr.get();
	printf("SCSI command reply is %zu bytes\n", iscsi_reply.second);
	if (WRITE(fd, iscsi_reply.first, iscsi_reply.second) == -1)
		ok = false;
	delete [] iscsi_reply.first;

	return ok;
}

void server::handler()
{
	scsi scsi_dev;

	for(;;) {
		int fd = accept(listen_fd, nullptr, nullptr);
		if (fd == -1)
			continue;

		iscsi_pdu_bhs bhs;

		session *s  = nullptr;
		bool     ok = true;

		do {
			uint8_t pdu[48] { 0 };
			if (READ(fd, pdu, sizeof pdu) == -1)
				break;

			bhs.set(pdu, sizeof pdu);

			printf("opcode: %02x / %s\n", bhs.get_opcode(), pdu_opcode_to_string(bhs.get_opcode()).c_str());

			uint8_t *ahs        = nullptr;
			size_t   ahs_length = bhs.get_ahs_length() * 4;
			printf("AHS size: %zu\n", ahs_length);
			if (ahs_length) {
				ahs = new uint8_t[ahs_length];

				if (READ(fd, ahs, ahs_length) == -1) {
					delete [] ahs;
					break;
				}
			}

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

			if (bhs.get_opcode() == iscsi_pdu_bhs::iscsi_bhs_opcode::o_login_req) {
				if (s)
					delete s;

				s = handle_login(pdu, { data, data_length }, fd);
				if (s == nullptr)
					ok = false;
			}
			else if (bhs.get_opcode() == iscsi_pdu_bhs::iscsi_bhs_opcode::o_scsi_cmd) {
				ok = handle_scsi_command(pdu, { data, data_length }, &scsi_dev, fd);
			}

			printf("\n");

			delete [] data;
			delete [] ahs;
		}
		while(ok);

		close(fd);

		delete s;
	}
}
