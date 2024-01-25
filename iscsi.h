#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>


typedef enum
{
	ir_as_is,  // only a sense and/or data pdu
	ir_empty_sense,  // only a possibly empty sense
	ir_r2t,  // R2T
} iscsi_reacion_t;

std::vector<std::string>     data_to_text_array(const uint8_t *const data, const size_t n);
std::pair<uint8_t *, size_t> text_array_to_data(const std::vector<std::string> & in);
