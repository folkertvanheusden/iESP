#include <cstdio>

#include "server.h"


int main(int argc, char *argv[])
{
	server s;
	s.begin();

	s.handler();

	return 0;
}
