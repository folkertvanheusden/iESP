#include <atomic>
#include "snmp/snmp.h"


void init_snmp(snmp **const snmp_, snmp_data **const snmp_data_, io_stats_t *const ios, iscsi_stats_t *const is, std::function<int(void *)> percentage_diskspace, void *const gpd_context, int *const cpu_usage, int *const ram_free_kb, std::atomic_bool *const stop, const int port);
