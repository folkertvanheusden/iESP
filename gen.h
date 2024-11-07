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

struct io_stats_t {
	uint64_t n_reads       { 0 };
	uint64_t bytes_read    { 0 };
	uint64_t n_writes      { 0 };
	uint64_t bytes_written { 0 };
	uint64_t n_syncs       { 0 };
	uint64_t n_trims       { 0 };
	// 1.3.6.1.4.1.2021.11.54: "The number of 'ticks' (typically 1/100s) spent waiting for IO."
	// https://www.circitor.fr/Mibs/Html/U/UCD-SNMP-MIB.php#ssCpuRawWait
	uint64_t io_wait       { 0 };

	io_stats_t() {
	}

	void reset() {
		n_reads = 0;
		bytes_read = 0;
		n_writes = 0;
		bytes_written = 0;
		n_syncs = 0;
		n_trims = 0;
		io_wait = 0;
	}
};
