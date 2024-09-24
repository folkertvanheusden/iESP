#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <getopt.h>
#include <unistd.h>
#include <sys/resource.h>

#include "backend-file.h"
#include "backend-nbd.h"
#include "com-sockets.h"
#include "log.h"
#include "server.h"
#include "snmp.h"
#include "utils.h"
#include "snmp/snmp.h"


std::atomic_bool stop { false };

void sigh(int sig)
{
	stop = true;
	DOLOG("Stop signal received\n");
}

uint64_t get_cpu_usage_us()
{
	rusage ru { };

	if (getrusage(RUSAGE_SELF, &ru) == 0) {
		return ru.ru_utime.tv_sec * 1000000 + ru.ru_utime.tv_usec +
		       ru.ru_stime.tv_sec * 1000000 + ru.ru_stime.tv_usec;
	}

	DOLOG("getrusage failed\n");

	return 0;
}

void maintenance_thread(std::atomic_bool *const stop, backend *const bf, int *const df_percentage, int *const cpu_usage, int *const ram_free_kb)
{
	uint64_t prev_df_poll = 0;
	uint64_t prev_w_poll  = 0;

	int prev_cpu_usage = get_cpu_usage_us();

	while(!*stop) {
		usleep(101000);

		uint64_t now = get_micros();

		if (now - prev_df_poll >= 30000000 && bf->is_idle()) {
			*df_percentage = bf->get_free_space_percentage();
			prev_df_poll   = now;
		}

		if (now - prev_w_poll >= 1000000) {
			int current_cpu_usage = get_cpu_usage_us();
			*cpu_usage = (current_cpu_usage - prev_cpu_usage) / 10000;
			prev_cpu_usage = current_cpu_usage;

			rlimit rlim { };
			rusage ru   { };
			if (getrlimit(RLIMIT_DATA, &rlim) == 0 && getrusage(RUSAGE_SELF, &ru) == 0)
				*ram_free_kb = rlim.rlim_max / 1024 - ru.ru_maxrss;
			else
				*ram_free_kb = 0;

			prev_w_poll = now;
		}
	}
}

void help()
{
	printf("-b x    backend type: file (default) or nbd (e.g. iscsi -> nbd proxy)\n");
	printf("-d x    device/file/host:port to serve (device/file: -b file, host:port: -b nbd)\n");
	printf("-t x    target name\n");
	printf("-i x    IP-address of adapter to listen on\n");
	printf("-p x    TCP-port to listen on\n");
	printf("-T x    trim level (0=disable, 1=normal (default), 2=auto)\n");
	printf("-S      enable SNMP agent\n");
	printf("-h      this help\n");
}

int main(int argc, char *argv[])
{
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT,  sigh);

	enum backend_type_t { BT_FILE, BT_NBD };

	std::string    ip_address = "0.0.0.0";
	int            port       = 3260;
	std::string    dev        = "test.dat";
	std::string    target_name= "test";
	int            trim_level = 1;
	bool           use_snmp   = false;
	backend_type_t bt         = backend_type_t::BT_FILE;
	int o = -1;
	while((o = getopt(argc, argv, "Sb:d:i:p:T:t:h")) != -1) {
		if (o == 'S')
			use_snmp = true;
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
		else {
			help();
			return o != 'h';
		}
	}

	char hostname[64] { 0 };
	gethostname(hostname, sizeof hostname);
	init_logger(hostname);

	io_stats_t    ios { };
	iscsi_stats_t is  { };

	int cpu_usage            = 0;
	int percentage_diskspace = 0;
	int ram_free_kb          = 0;

	snmp      *snmp_      { nullptr };
	snmp_data *snmp_data_ { nullptr };
	if (use_snmp)
		init_snmp(&snmp_, &snmp_data_, &ios, &is, &percentage_diskspace, &cpu_usage, &ram_free_kb, &stop);

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
	scsi sd(b, trim_level, &ios);

	com_sockets c(ip_address, port, &stop);
	if (c.begin() == false) {
		fprintf(stderr, "Failed to communication layer\n");
		return 1;
	}

	std::thread *mth = new std::thread(maintenance_thread, &stop, b, &percentage_diskspace, &cpu_usage, &ram_free_kb);

	server s(&sd, &c, &is, target_name);
	printf("Go!\n");
	s.handler();

	delete snmp_;

	mth->join();
	delete mth;

	delete b;

	return 0;
}
