#include <atomic>
#include <csignal>
#include <cstdio>

#include "backend-file.h"
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

	server s(&bf, "192.168.64.206", 3260);
	//server s(&bf, "127.0.0.1", 3260);
	s.begin();

	s.handler();

	return 0;
}
