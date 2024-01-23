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
