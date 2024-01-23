#include <csignal>
#include <cstdio>

#include "server.h"


int main(int argc, char *argv[])
{
	signal(SIGPIPE, SIG_IGN);

	server s;
	s.begin();

	s.handler();

	return 0;
}
