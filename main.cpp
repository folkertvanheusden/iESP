#include <atomic>
#include <csignal>
#include <cstdio>

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

int main(int argc, char *argv[])
{
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, sigh);

	backend_file bf("test.dat");

	com_sockets c("192.168.64.206", 3260, &stop);
	if (c.begin() == false) {
		fprintf(stderr, "Failed to communication layer\n");
		return 1;
	}

	server s(&bf, &c);
	s.handler();

	return 0;
}
