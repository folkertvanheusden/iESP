#include <atomic>
#include <csignal>
#include <cstdio>
#include <getopt.h>
#include <unistd.h>

#include "backend-file.h"
#include "com-sockets.h"
#include "log.h"
#include "server.h"


std::atomic_bool stop { false };

void sigh(int sig)
{
	stop = true;
	DOLOG("Stop signal received\n");
}

void help()
{
	printf("-d x    device/file to serve\n");
	printf("-i x    IP-address of adapter to listen on\n");
	printf("-p x    TCP-port to listen on\n");
	printf("-T x    trim level (0=disable, 1=normal (default), 2=auto)\n");
	printf("-h      this help\n");
}

int main(int argc, char *argv[])
{
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT,  sigh);

	std::string ip_address = "192.168.64.206";
	int         port       = 3260;
	std::string dev        = "test.dat";
	int         trim_level = 1;
	int o = -1;
	while((o = getopt(argc, argv, "d:i:p:T:h")) != -1) {
		if (o == 'd')
			dev = optarg;
		else if (o == 'i')
			ip_address = optarg;
		else if (o == 'p')
			port = atoi(optarg);
		else if (o == 'T')
			trim_level = atoi(optarg);
		else {
			help();
			return o != 'h';
		}
	}

	char hostname[64] { 0 };
	gethostname(hostname, sizeof hostname);
	init_logger(hostname);

	io_stats_t is { };

	backend_file bf(dev);
	if (bf.begin() == false) {
		fprintf(stderr, "Failed to initialize storage backend\n");
		return 1;
	}
	scsi sd(&bf, trim_level, &is);

	com_sockets c(ip_address, port, &stop);
	if (c.begin() == false) {
		fprintf(stderr, "Failed to communication layer\n");
		return 1;
	}

	server s(&sd, &c);
	s.handler();

	return 0;
}
