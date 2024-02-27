#include <atomic>
#include "snmp/snmp.h"


void init_snmp(snmp **const snmp_, snmp_data **const snmp_data_, io_stats_t *const ios, iscsi_stats_t *const is, int *const percentage_diskspace, int *const cpu_usage, std::atomic_bool *const stop);
