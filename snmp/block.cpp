#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "block.h"


block::block(const uint8_t *const data, const size_t len, const bool is_move) :
	len(len)
{
	if (is_move)
		this->data = data;
	else {
		this->data = new uint8_t[len];
		memcpy(const_cast<uint8_t *>(this->data), data, len);
	}
}

block::block(const std::vector<uint8_t> & data) :
	data(new uint8_t[data.size()]),
	len(data.size())
{
	memcpy(const_cast<uint8_t *>(this->data), data.data(), len);
}

block::block(const block & other) :
	data(new uint8_t[other.get_size()]),
	len(other.get_size())
{
	memcpy(const_cast<uint8_t *>(this->data), other.get_data(), len);
}
							    
block::~block()
{
	delete [] data;
}

bool block::empty() const
{
	return get_size() == 0;
}

size_t block::get_size() const
{
	return len;
}

const uint8_t * block::get_data() const
{
	return data;
}

bool block::operator==(const block & other) const
{
	if (other.get_size() != get_size())
		return false;

	return memcmp(other.get_data(), get_data(), get_size()) == 0;
}

bool block::operator!=(const block & other) const
{
	if (other.get_size() != get_size())
		return true;

	return memcmp(other.get_data(), get_data(), get_size()) != 0;
}

void block::push_to_vector(std::vector<uint8_t> & to) const
{
	size_t org_size = to.size();
	size_t add_size = get_size();
	if (add_size > 0) {
		to.resize(org_size + add_size);
		memcpy(to.data() + org_size, get_data(), add_size);
	}
}

std::string block::dump() const
{
	std::string out;
	size_t add_size = get_size();
	if (add_size > 0) {
		out.resize(add_size * 3 - 1);
		for(size_t i=0, o=0; i<add_size;) {
			if (o)
				out[o++] = ' ';
			uint8_t v = data[i++];
			uint8_t n1 = v >> 4;
			uint8_t n2 = v & 15;
			out[o++] = (n1 > 9 ? 'a' - 10 : '0') + n1;
			out[o++] = (n2 > 9 ? 'a' - 10 : '0') + n2;
		}
	}
	return out;
}

block *block::substr(const size_t offset) const
{
	if (offset >= get_size())
		return nullptr;
	return new block(get_data() + offset, get_size() - offset);
}

block *block::substr(const size_t offset, const size_t n) const
{
	if (offset + n >= get_size())
		return nullptr;
	return new block(get_data() + offset, n);
}

block *block::duplicate() const
{
        size_t   n    = get_size();
        uint8_t *copy = new uint8_t[n];
        memcpy(copy, get_data(), n);
        return new block(copy, n, true);
}

uint8_t block::get_byte()
{
#if !defined(TEENSY4_1)
	if (offset == get_size())
		throw std::range_error("block::get_byte");
#endif

	return data[offset++];
}

void block::get_bytes(const size_t len, uint8_t *const to)
{
#if !defined(TEENSY4_1)
	if (offset + len > get_size())
		throw std::range_error("block::get_bytes");
#endif

	memcpy(to, &data[offset], len);
	offset += len;
}

block block::get_bytes(const size_t len)
{
#if !defined(TEENSY4_1)
	if (offset + len > get_size())
		throw std::range_error("block::get_bytes");
#endif

        uint8_t *copy = new uint8_t[len];
        memcpy(copy, &data[offset], len);
	offset += len;
        return block(copy, len, true);
}

size_t block::get_bytes_left() const
{
	return get_size() - offset;
}

void block::skip_bytes(const size_t len)
{
#if !defined(TEENSY4_1)
	if (offset + len > get_size())
		throw std::range_error("block::skip_bytes");
#endif

	offset += len;
}

bool block::is_empty() const
{
	return offset == len;
}
