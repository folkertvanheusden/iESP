#include <atomic>
#include "scsi.h"
#include "server.h"
#if defined(ARDUINO)
#include "version.h"
#endif
#include "snmp/snmp.h"


void init_snmp(snmp **const snmp_, snmp_data **const snmp_data_, io_stats_t *const ios, iscsi_stats_t *const is, std::function<int(void *)> get_percentage_diskspace, void *const gpd_context, int *const cpu_usage, int *const ram_free_kb, std::atomic_bool *const stop)
{
	*snmp_data_ = new snmp_data();
	(*snmp_data_)->register_oid("1.3.6.1.4.1.2021.13.15.1.1.2", "iESP"  );
	(*snmp_data_)->register_oid("1.3.6.1.2.1.1.1.0",            "iESP"  );
	//(*snmp_data_)->register_oid("1.3.6.1.2.1.1.2.0", new snmp_data_type_oid("1.3.6.1.4.1.57850.1"));
	(*snmp_data_)->register_oid("1.3.6.1.2.1.1.3.0",            new snmp_data_type_running_since());
	(*snmp_data_)->register_oid("1.3.6.1.2.1.1.4.0",            "Folkert van Heusden <mail@vanheusden.com>");
	(*snmp_data_)->register_oid("1.3.6.1.2.1.1.5.0",            "iESP");
	(*snmp_data_)->register_oid("1.3.6.1.2.1.1.6.0",            "The Netherlands, Europe, Earth");
	(*snmp_data_)->register_oid("1.3.6.1.2.1.1.7.0",            snmp_integer::si_integer, 254);
	(*snmp_data_)->register_oid("1.3.6.1.2.1.1.8.0",            snmp_integer::si_integer, 0);
#if defined(ARDUINO)
	(*snmp_data_)->register_oid("1.3.6.1.4.1.2021.100.2",       version_str);
#endif
	(*snmp_data_)->register_oid("1.3.6.1.4.1.2021.100.3",       __DATE__);
	(*snmp_data_)->register_oid("1.3.6.1.4.1.2021.100.1",       snmp_integer::snmp_integer_type::si_integer, 1);

	(*snmp_data_)->register_oid("1.3.6.1.4.1.2021.13.15.1.1.3", new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter64, &ios->bytes_read   ));
	(*snmp_data_)->register_oid("1.3.6.1.4.1.2021.13.15.1.1.4", new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter64, &ios->bytes_written));
	(*snmp_data_)->register_oid("1.3.6.1.4.1.2021.13.15.1.1.5", new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter64, &ios->n_reads      ));
	(*snmp_data_)->register_oid("1.3.6.1.4.1.2021.13.15.1.1.6", new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter64, &ios->n_writes     ));
	(*snmp_data_)->register_oid("1.3.6.1.4.1.2021.11.54",       new snmp_data_type_stats_uint32_t(&ios->io_wait));
	(*snmp_data_)->register_oid("1.3.6.1.2.1.142.1.1.1.1.10",   new snmp_data_type_stats_uint32_t(&is->iscsiInstSsnFailures));
	(*snmp_data_)->register_oid("1.3.6.1.2.1.142.1.10.2.1.1",   new snmp_data_type_stats_uint32_t(&is->iscsiSsnCmdPDUs));
	(*snmp_data_)->register_oid("1.3.6.1.2.1.142.1.6.2.1.1",    new snmp_data_type_stats_uint32_t(&is->iscsiTgtLoginAccepts));
	(*snmp_data_)->register_oid("1.3.6.1.2.1.142.1.6.3.1.1",    new snmp_data_type_stats_uint32_t(&is->iscsiTgtLogoutNormals));
	(*snmp_data_)->register_oid("1.3.6.1.2.1.142.1.1.2.1.1",    new snmp_data_type_stats_uint32_t(&is->iscsiInstSsnDigestErrors));
	(*snmp_data_)->register_oid("1.3.6.1.2.1.142.1.10.2.1.3",   new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter64, &is->iscsiSsnTxDataOctets));
	(*snmp_data_)->register_oid("1.3.6.1.2.1.142.1.10.2.1.4",   new snmp_data_type_stats(snmp_integer::snmp_integer_type::si_counter64, &is->iscsiSsnRxDataOctets));
	(*snmp_data_)->register_oid("1.3.6.1.2.1.142.1.1.2.1.3",    new snmp_data_type_stats_uint32_t(&is->iscsiInstSsnFormatErrors));
	(*snmp_data_)->register_oid("1.3.6.1.4.1.2021.9.1.9.1",     new snmp_data_type_stats_int_callback(get_percentage_diskspace, gpd_context));
	(*snmp_data_)->register_oid("1.3.6.1.4.1.2021.11.9.0",      new snmp_data_type_stats_int(cpu_usage));
	(*snmp_data_)->register_oid("1.3.6.1.4.1.2021.4.11.0",      new snmp_data_type_stats_int(ram_free_kb));

	*snmp_ = new snmp(*snmp_data_, stop);
}

