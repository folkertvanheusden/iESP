#pragma once

#include <cstdint>
#include <string>
#include <vector>


class block {
private:
	const uint8_t *data   { nullptr };
	      size_t   len    { 0       };
	      size_t   offset { 0       };

public:
	block(const uint8_t *const data, const size_t len, const bool move = false);
	block(const std::vector<uint8_t> & data);
	block(const block & other);
	virtual ~block();

	bool            operator==(const block & other)               const;
	bool            operator!=(const block & other)               const;
	bool            empty()                                       const;
	size_t          get_size()                                    const;
	const uint8_t * get_data()                                    const;
	void            push_to_vector(std::vector<uint8_t> & to)     const;
	std::string     dump()                                        const;
	block         * substr(const size_t offset)                   const;
	block         * substr(const size_t offset, const size_t len) const;
	block         * duplicate()                                   const;

	uint8_t         get_byte();
	void            get_bytes(const size_t len, uint8_t *const to);
	block           get_bytes(const size_t len);
	size_t          get_bytes_left()                              const;
	void            skip_bytes(const size_t len);
	bool            is_empty()                                    const;
};
