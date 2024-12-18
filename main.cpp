#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <getopt.h>
#include <unistd.h>
#if !defined(__MINGW32__)
#include <sys/resource.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "backend-file.h"
#include "backend-nbd.h"
#include "com-sockets.h"
#include "log.h"
#include "random.h"
#include "server.h"
#include "snmp.h"
#include "utils.h"
#include "snmp/snmp.h"


std::atomic_bool stop { false };

void sigh(int sig)
{
	stop = true;
	DOLOG(logging::ll_info, "sigh", "-", "stop signal received");
}

uint64_t get_cpu_usage_us()
{
#if !defined(__MINGW32__)
	rusage ru { };

	if (getrusage(RUSAGE_SELF, &ru) == 0) {
		return ru.ru_utime.tv_sec * 1000000 + ru.ru_utime.tv_usec +
		       ru.ru_stime.tv_sec * 1000000 + ru.ru_stime.tv_usec;
	}

	DOLOG(logging::ll_error, "get_cpu_usage_us", "-", "getrusage failed: %s", strerror(errno));
#endif

	return 0;
}

void maintenance_thread(backend *const b, backend_stats_t *const bs, std::atomic_bool *const stop, int *const cpu_usage, int *const ram_free_kb)
{
	uint64_t prev_w_poll   = 0;

	int prev_cpu_usage = get_cpu_usage_us();

	while(!*stop) {
		sleep(1);

		uint64_t now = get_micros();
		if (now - prev_w_poll >= 1000000) {
			int current_cpu_usage = get_cpu_usage_us();
			*cpu_usage = (current_cpu_usage - prev_cpu_usage) / 10000;
			prev_cpu_usage = current_cpu_usage;

#if !defined(__MINGW32__)
			rlimit rlim { };
			rusage ru   { };
			if (getrlimit(RLIMIT_DATA, &rlim) == 0 && getrusage(RUSAGE_SELF, &ru) == 0)
				*ram_free_kb = rlim.rlim_max / 1024 - ru.ru_maxrss;
			else
#endif
				*ram_free_kb = 0;

			prev_w_poll = now;
		}

		b->get_and_reset_stats(bs);
		bs->io_wait_ticks = bs->io_wait * 10000;
	}
}

int get_diskspace(void *const context)
{
	backend *const bf = reinterpret_cast<backend *>(context);

	return bf->get_free_space_percentage();
}

void help()
{
	printf("-b x    backend type: file (default) or nbd (e.g. iscsi -> nbd proxy)\n");
	printf("-d x    device/file/host:port to serve (device/file: -b file, host:port: -b nbd)\n");
	printf("-t x    target name\n");
	printf("-i x    IP-address of adapter to listen on\n");
	printf("-p x    TCP-port to listen on\n");
	printf("-T x    trim level (0=disable, 1=normal (default), 2=auto)\n");
	printf("-L x,y  set file log level (x) and screen log level (y)\n");
	printf("-l x    set log file\n");
	printf("-D      disable digest\n");
	printf("-S x    enable SNMP agent on port x, usually 161\n");
	printf("-P x    write PID-file\n");
#if !defined(__MINGW32__)
	printf("-f      become daemon process\n");
#endif
	printf("-h      this help\n");
}

int main(int argc, char *argv[])
{
#if !defined(__MINGW32__)
	signal(SIGPIPE, SIG_IGN);
#endif
	signal(SIGINT,  sigh);
	signal(SIGTERM, sigh);

#if defined(__MINGW32__)
	WSADATA wsaData { };
	int result = WSAStartup(MAKEWORD(2,2), &wsaData);
	if (result != 0) {
		printf("WSAStartup failed: %d\n", result);
		return 1;
	}
#endif

	enum backend_type_t { BT_FILE, BT_NBD };

	bool           do_daemon  = false;
	std::string    pid_file;
	std::string    ip_address = "0.0.0.0";
	int            port       = 3260;
	std::string    dev        = FILENAME;
	std::string    target_name= "test";
	int            trim_level = 1;
	bool           use_snmp   = false;
	int            snmp_port  = 161;
	bool           digest_chk = true;
	backend_type_t bt         = backend_type_t::BT_FILE;
	const char    *logfile    = "/tmp/iesp.log";
	logging::log_level_t ll_screen = logging::ll_error;
	logging::log_level_t ll_file   = logging::ll_error;
	int o = -1;
	while((o = getopt(argc, argv, "P:fS:Db:d:i:p:T:t:L:l:h")) != -1) {
		if (o == 'P')
			pid_file = optarg;  // used for scripting
		else if (o == 'f')
			do_daemon = true;
		else if (o == 'S') {
			use_snmp = true;
			snmp_port = atoi(optarg);
		}
		else if (o == 'D')
			digest_chk = false;
		else if (o == 'b') {
			if (strcasecmp(optarg, "file") == 0)
				bt = backend_type_t::BT_FILE;
			else if (strcasecmp(optarg, "nbd") == 0)
				bt = backend_type_t::BT_NBD;
			else {
				fprintf(stderr, "-b expects either \"file\" or \"nbd\"\n");
				return 1;
			}
		}
		else if (o == 'd')
			dev = optarg;
		else if (o == 'i')
			ip_address = optarg;
		else if (o == 'p')
			port = atoi(optarg);
		else if (o == 'T')
			trim_level = atoi(optarg);
		else if (o == 't')
			target_name = optarg;
		else if (o == 'L') {
			auto parts = split(optarg, ",");
			if (parts.size() != 2) {
				fprintf(stderr, "Argument missing for -L (file,screen)\n");
				return 1;
			}

			ll_screen  = logging::parse_ll(parts[0]);
			ll_file    = logging::parse_ll(parts[1]);
		}
		else if (o == 'l')
			logfile = optarg;
		else {
			help();
			return o != 'h';
		}
	}

	logging::initlogger();
	logging::setlog(logfile, ll_file, ll_screen);

	init_my_getrandom();

	iscsi_stats_t is { };

	backend *b = nullptr;

	if (bt == backend_type_t::BT_FILE)
		b = new backend_file(dev);
	else if (bt == backend_type_t::BT_NBD) {
		std::string::size_type colon = dev.find(":");
		if (colon == std::string::npos) {
			fprintf(stderr, "NBD: port missing\n");
			return 1;
		}

		b = new backend_nbd(dev.substr(0, colon), std::stoi(dev.substr(colon + 1)));
	}

	if (b->begin() == false) {
		fprintf(stderr, "Failed to initialize storage backend\n");
		return 1;
	}
	scsi sd(b, trim_level);

	com_sockets c(ip_address, port, &stop);
	if (c.begin() == false) {
		fprintf(stderr, "Failed to setup communication layer\n");
		return 1;
	}

	printf("Go!\n");

#if !defined(__MINGW32__)
	if (do_daemon) {
		if (daemon(-1, -1) == -1) {
			fprintf(stderr, "Failed to daemonize: %s\n", strerror(errno));
			return 1;
		}
	}
#endif

	backend_stats_t bs          {         };
	int             cpu_usage   { 0       };
	int             ram_free_kb { 0       };
	snmp           *snmp_       { nullptr };
	snmp_data      *snmp_data_  { nullptr };
	if (use_snmp)
		init_snmp(&snmp_, &snmp_data_, &is, get_diskspace, b, &bs, &cpu_usage, &ram_free_kb, &stop, snmp_port);

	server s(&sd, &c, &is, target_name, digest_chk);

	std::thread *mth = new std::thread(maintenance_thread, b, &bs, &stop, &cpu_usage, &ram_free_kb);

	if (pid_file.empty() == false) {
		FILE *fh = fopen(pid_file.c_str(), "w");
		if (!fh) {
			fprintf(stderr, "Failed to create \"%s\": %s\n", pid_file.c_str(), strerror(errno));
			return 1;
		}

		fprintf(fh, "%d\n", getpid());
		fclose(fh);
	}

	s.handler();

	delete snmp_;

	mth->join();
	delete mth;

	delete b;

	if (pid_file.empty() == false) {
		if (unlink(pid_file.c_str()) == -1)
			fprintf(stderr, "Failed to remove \"%s\": %s\n", pid_file.c_str(), strerror(errno));
	}

	return 0;
}
