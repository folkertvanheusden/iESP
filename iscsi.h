#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>


typedef struct {
	uint8_t *data;
	size_t n;
} blob_t;

struct r2t_session {
	uint64_t buffer_lba;
	uint32_t offset_from_lba;
	uint32_t bytes_left;
	blob_t   PDU_initiator;
};

typedef enum
{
	ir_as_is,  // only a sense and/or data pdu
	ir_empty_sense,  // only a possibly empty sense
	ir_r2t,  // R2T
} iscsi_reacion_t;

std::vector<std::string>     data_to_text_array(const uint8_t *const data, const size_t n);
std::pair<uint8_t *, size_t> text_array_to_data(const std::vector<std::string> & in);
void                         set_bits(uint8_t *const target, const int bit_nr, const int length, const uint8_t value);
uint8_t                      get_bits(const uint8_t from, const int bit_nr, const int length);
