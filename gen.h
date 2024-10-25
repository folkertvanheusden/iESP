#pragma once

#include <cstdint>

#define DEFAULT_SERIAL "12345678"

#define FILENAME "test.dat"

static_assert(sizeof(size_t) >= 4);
static_assert(sizeof(int) >= 2);

struct data_descriptor {
	uint64_t lba;
	uint32_t n_sectors;
};
