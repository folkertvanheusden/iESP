#pragma once

#include <cstdint>

static_assert(sizeof(size_t) >= 4);
static_assert(sizeof(int) >= 2);

struct data_descriptor {
	uint64_t lba;
	uint32_t n_sectors;
};
