#pragma once

#include <cstdint>

#define DEFAULT_SERIAL "12345678"
#define FILENAME       "test.dat"

static_assert(sizeof(size_t) >= 4);
static_assert(sizeof(int) >= 2);

struct data_descriptor {
	uint64_t lba;
	uint32_t n_sectors;
};

typedef struct {
	uint64_t n_reads;
	uint64_t bytes_read;
	uint64_t n_writes;
	uint64_t bytes_written;
	uint64_t n_syncs;
	uint64_t n_trims;
	// 1.3.6.1.4.1.2021.11.54: "The number of 'ticks' (typically 1/100s) spent waiting for IO."
	// https://www.circitor.fr/Mibs/Html/U/UCD-SNMP-MIB.php#ssCpuRawWait
	uint32_t io_wait;
} io_stats_t;
