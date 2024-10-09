#include <cassert>
#include <cstring>

#include "iscsi.h"


std::vector<std::string> data_to_text_array(const uint8_t *const data, const size_t n)
{
	std::vector<std::string> out;

	std::string pair;
	for(size_t i=0; i<n; i++) {
		if (data[i] == 0x00) {
			out.push_back(pair);
			pair.clear();
		}
		else {
			pair += char(data[i]);
		}
	}

	if (pair.empty() == false)
		out.push_back(pair);

	return out;
}

std::pair<uint8_t *, size_t> text_array_to_data(const std::vector<std::string> & kvs)
{
	// determine total length
	size_t len = 0;
	for(const auto & kv: kvs)
		len += kv.size() + 1;

	// generate
	uint8_t *p = new uint8_t[len + 4/*padding*/]();
	size_t offset = 0;
	for(const auto & kv: kvs) {
		memcpy(&p[offset], kv.c_str(), kv.size());
		offset += kv.size() + 1;  // for 0x00
	}
	assert(offset == len);

	return { p, len };
}

void set_bits(uint8_t *const target, const int bit_nr, const int length, const uint8_t value)
{
	uint16_t max_value = (1 << length) - 1;
	assert(value <= max_value);
	(*target) &= ~(max_value << bit_nr);
	(*target) |= value << bit_nr;
}

uint8_t get_bits(const uint8_t from, const int bit_nr, const int length)
{
	uint16_t mask = (1 << length) - 1;
	return (from >> bit_nr) & mask; 
}

// first : finished CRC32c
// second: to be used for incremental
std::pair<uint32_t, uint32_t> crc32_0x11EDC6F41(const uint8_t *data, const size_t len, std::optional<uint32_t> start_with)
{
	uint32_t r = start_with.has_value() ? start_with.value() : ~0;

	for(size_t k=0; k<len; k++) {
		r ^= data[k];

		for(int i = 0; i < 8; i++) {
			uint32_t t = ~((r & 1) - 1);
			r = (r >> 1) ^ (0x82f63b78 & t);  // 0x82f63b78 = 0x11EDC6F41 in reverse bit order
		}
	}

	return { ~r, r };
}
