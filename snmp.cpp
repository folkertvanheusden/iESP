#include <atomic>
#include <functional>

#include "backend.h"
#include "log.h"
#include "scsi.h"
#include "server.h"
#if defined(ARDUINO)
#include "version.h"
#endif
#include "snmp/snmp.h"


void init_snmp(snmp **const snmp_, snmp_data **const snmp_data_, iscsi_stats_t *const is, std::function<int(void *)> get_percentage_diskspace, void *const gpd_context, backend_stats_t *const bs, int *const cpu_usage, int *const ram_free_kb, std::atomic_bool *const stop, const int port)
{
	*snmp_data_ = new snmp_data();
	(*snmp_data_)->register_oid("1.3.6.1.2.1.1.1.0",            "iESP"  );
	//(*snmp_data_)->register_oid("1.3.6.1.2.1.1.2.0", new snmp_data_type_oid("1.3.6.1.4.1.57850.1"));
	(*snmp_data_)->register_oid("1.3.6.1.2.1.1.3.0",            new snmp_data_type_running_since());
	(*snmp_data_)->register_oid("1.3.6.1.2.1.1.4.0",            "Folkert van Heusden <mail@vanheusden.com>");
	(*snmp_data_)->register_oid("1.3.6.1.2.1.1.5.0",            "iESP");
	(*snmp_data_)->register_oid("1.3.6.1.2.1.1.6.0",            "The Netherlands, Europe, Earth");
	(*snmp_data_)->register_oid("1.3.6.1.2.1.1.7.0",            snmp_integer::si_integer, 254);
	(*snmp_data_)->register_oid("1.3.6.1.2.1.1.8.0",            snmp_integer::si_ticks, 0);
	(*snmp_data_)->register_oid("1.3.6.1.2.1.142.1.1.1.1.10",   new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter32, &is->iscsiInstSsnFailures));
	(*snmp_data_)->register_oid("1.3.6.1.2.1.142.1.1.2.1.1",    new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter32, &is->iscsiInstSsnDigestErrors));
	(*snmp_data_)->register_oid("1.3.6.1.2.1.142.1.1.2.1.3",    new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter32, &is->iscsiInstSsnFormatErrors));
	(*snmp_data_)->register_oid("1.3.6.1.2.1.142.1.6.2.1.1",    new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter32, &is->iscsiTgtLoginAccepts));
	(*snmp_data_)->register_oid("1.3.6.1.2.1.142.1.6.3.1.1",    new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter32, &is->iscsiTgtLogoutNormals));
	(*snmp_data_)->register_oid("1.3.6.1.2.1.142.1.10.2.1.1",   new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter32, &is->iscsiSsnCmdPDUs));
	(*snmp_data_)->register_oid("1.3.6.1.2.1.142.1.10.2.1.3",   new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter64, &is->iscsiSsnTxDataOctets));
	(*snmp_data_)->register_oid("1.3.6.1.2.1.142.1.10.2.1.4",   new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter64, &is->iscsiSsnRxDataOctets));
#if !defined(TEENSY4_1)
	(*snmp_data_)->register_oid("1.3.6.1.4.1.2021.11.54",       new snmp_data_type_stats_uint32_t(&bs->io_wait_ticks));
#endif
	(*snmp_data_)->register_oid("1.3.6.1.4.1.2021.13.15.1.1.2", "iESP"  );
	(*snmp_data_)->register_oid("1.3.6.1.4.1.2021.100.1",       snmp_integer::snmp_integer_type::si_integer, 1);
#if defined(ARDUINO)
	(*snmp_data_)->register_oid("1.3.6.1.4.1.2021.100.2",       version_str);
#endif
	(*snmp_data_)->register_oid("1.3.6.1.4.1.2021.100.3",       __DATE__);

	(*snmp_data_)->register_oid("1.3.6.1.4.1.2021.13.15.1.1.3", new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter64, &bs->bytes_read   ));
	(*snmp_data_)->register_oid("1.3.6.1.4.1.2021.13.15.1.1.4", new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter64, &bs->bytes_written));
	(*snmp_data_)->register_oid("1.3.6.1.4.1.2021.13.15.1.1.5", new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter64, &bs->n_reads      ));
	(*snmp_data_)->register_oid("1.3.6.1.4.1.2021.13.15.1.1.6", new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter64, &bs->n_writes     ));
	(*snmp_data_)->register_oid("1.3.6.1.4.1.2021.4.11.0",      new snmp_data_type_stats_int(ram_free_kb));
	(*snmp_data_)->register_oid("1.3.6.1.4.1.2021.9.1.9.1",     new snmp_data_type_stats_int_callback(get_percentage_diskspace, gpd_context));
	(*snmp_data_)->register_oid("1.3.6.1.4.1.2021.11.9.0",      new snmp_data_type_stats_int(cpu_usage));

	// TODO bs-> bla  in snmp

	*snmp_ = new snmp(*snmp_data_, stop, 1, port);
	if ((*snmp_)->begin() == false)
		DOLOG(logging::ll_error, "snmp", "-", "failed to initialize SNMP server");
}
