#include <atomic>
#include <csignal>
#include <cstdio>
#include <getopt.h>
#include <unistd.h>

#include "backend-file.h"
#include "com-sockets.h"
#include "log.h"
#include "server.h"
#include "utils.h"
#include "snmp/snmp.h"


std::atomic_bool stop { false };

void sigh(int sig)
{
	stop = true;
	DOLOG("Stop signal received\n");
}

void maintenance_thread(std::atomic_bool *const stop, backend *const bf, uint64_t *const df_percentage)
{
	uint64_t prev_df_poll = 0;

	while(!*stop) {
		usleep(101000);

		uint64_t now = get_micros();
		if (now - prev_df_poll >= 30000000) {
			*df_percentage = bf->get_free_space_percentage();
			prev_df_poll   = now;
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

	uint64_t percentage_diskspace = 0;

	snmp_data snmp_data_;
	snmp *snmp_ = nullptr;
	if (use_snmp) {
		snmp_data_.register_oid("1.3.6.1.4.1.2021.13.15.1.1.2", "iESP"  );
		snmp_data_.register_oid("1.3.6.1.2.1.1.1.0", "iESP"  );
		//snmp_data_.register_oid("1.3.6.1.2.1.1.2.0", new snmp_data_type_oid("1.3.6.1.4.1.57850.1"));
		snmp_data_.register_oid("1.3.6.1.2.1.1.3.0", new snmp_data_type_running_since());
		snmp_data_.register_oid("1.3.6.1.2.1.1.4.0", "Folkert van Heusden <mail@vanheusden.com>");
		snmp_data_.register_oid("1.3.6.1.2.1.1.5.0", "iESP");
		snmp_data_.register_oid("1.3.6.1.2.1.1.6.0", "The Netherlands, Europe, Earth");
		snmp_data_.register_oid("1.3.6.1.2.1.1.7.0", snmp_integer::si_integer, 254);
		snmp_data_.register_oid("1.3.6.1.2.1.1.8.0", snmp_integer::si_integer, 0);

		snmp_data_.register_oid("1.3.6.1.4.1.2021.100.3", __DATE__);
		snmp_data_.register_oid("1.3.6.1.4.1.2021.100.1", snmp_integer::snmp_integer_type::si_integer, 1);

		snmp_data_.register_oid("1.3.6.1.4.1.2021.13.15.1.1.3", new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter64, &ios.n_reads      ));
		snmp_data_.register_oid("1.3.6.1.4.1.2021.13.15.1.1.4", new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter64, &ios.n_writes     ));
		snmp_data_.register_oid("1.3.6.1.4.1.2021.13.15.1.1.5", new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter64, &ios.bytes_read   ));
		snmp_data_.register_oid("1.3.6.1.4.1.2021.13.15.1.1.6", new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter64, &ios.bytes_written));
		snmp_data_.register_oid("1.3.6.1.4.1.2021.11.54",       new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter32, &ios.io_wait      ));
		snmp_data_.register_oid("1.3.6.1.2.1.142.1.10.2.1.1",   new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter32, &is.iscsiSsnCmdPDUs));
		snmp_data_.register_oid("1.3.6.1.2.1.142.1.10.2.1.3",   new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter64, &is.iscsiSsnTxDataOctets));
		snmp_data_.register_oid("1.3.6.1.2.1.142.1.10.2.1.4",   new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter64, &is.iscsiSsnRxDataOctets));
		snmp_data_.register_oid("1.3.6.1.4.1.2021.9.1.9.1",     new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_integer,   &percentage_diskspace));

		snmp_ = new snmp(&snmp_data_, &stop);
	}

	/*
	snmp.addIntegerHandler(".1.3.6.1.4.1.2021.11.9.0",  &cpu_usage           );
	snmp.addIntegerHandler(".1.3.6.1.4.1.2021.4.11.0",  &ram_free_kb         );
	*/

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

	std::thread *mth = new std::thread(maintenance_thread, &stop, &bf, &percentage_diskspace);

	server s(&sd, &c, &is);
	s.handler();

	delete snmp_;

	mth->join();
	delete mth;

	return 0;
}
