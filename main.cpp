#include <atomic>
#include <csignal>
#include <cstdio>
#include <getopt.h>
#include <unistd.h>
#include <sys/resource.h>

#include "backend-file.h"
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

void maintenance_thread(std::atomic_bool *const stop, backend *const bf, int *const df_percentage, int *const cpu_usage)
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

			prev_w_poll = now;
		}
	}
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
	bool        use_snmp   = false;
	int o = -1;
	while((o = getopt(argc, argv, "Sd:i:p:T:h")) != -1) {
		if (o == 'd')
			dev = optarg;
		else if (o == 'i')
			ip_address = optarg;
		else if (o == 'p')
			port = atoi(optarg);
		else if (o == 'T')
			trim_level = atoi(optarg);
		else if (o == 'S')
			use_snmp = true;
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

	snmp      *snmp_      { nullptr };
	snmp_data *snmp_data_ { nullptr };
	if (use_snmp)
		init_snmp(&snmp_, &snmp_data_, &ios, &is, &percentage_diskspace, &cpu_usage, &stop);

	backend_file bf(dev);
	if (bf.begin() == false) {
		fprintf(stderr, "Failed to initialize storage backend\n");
		return 1;
	}
	scsi sd(&bf, trim_level, &ios);

	com_sockets c(ip_address, port, &stop);
	if (c.begin() == false) {
		fprintf(stderr, "Failed to communication layer\n");
		return 1;
	}

	std::thread *mth = new std::thread(maintenance_thread, &stop, &bf, &percentage_diskspace, &cpu_usage);

	server s(&sd, &c, &is);
	s.handler();

	delete snmp_;

	mth->join();
	delete mth;

	return 0;
}
