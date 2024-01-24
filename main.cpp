#include <csignal>
#include <cstdio>

#include "backend-file.h"
#include "server.h"


int main(int argc, char *argv[])
{
	signal(SIGPIPE, SIG_IGN);

	backend_file bf("test.dat");

	server s(&bf);
	s.begin();

	s.handler();

	return 0;
}
