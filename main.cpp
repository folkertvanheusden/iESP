#include <csignal>
#include <cstdio>

#include "backend-file.h"
#include "server.h"


int main(int argc, char *argv[])
{
	signal(SIGPIPE, SIG_IGN);

	backend_file bf("test.dat");

	server s(&bf, "192.168.64.206", 3260);
	//server s(&bf, "127.0.0.1", 3260);
	s.begin();

	s.handler();

	return 0;
}
