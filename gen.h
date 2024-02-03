#pragma once

#include <cstdint>


struct data_descriptor {
	uint64_t lba;
	uint32_t n_sectors;
};
