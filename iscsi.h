#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>


#if defined(ARDUINO)
#define MAX_DATA_SEGMENT_SIZE 8192  // 8 kB, less fails with at least the libiscsi test-set
#else
#define MAX_DATA_SEGMENT_SIZE (256 * 1024 * 1024)  // 256 MB
#endif

enum residual { iSR_OVERFLOW, iSR_UNDERFLOW, iSR_OK };  // iSR: iS(CSI) Residual

enum iscsi_fail_reason { IFR_OK, IFR_CONNECTION, IFR_INVALID_FIELD, IFR_DIGEST, IFR_IO_ERROR, IFR_MISC, IFR_INVALID_COMMAND };

typedef struct {
	uint8_t *data;
	size_t n;
} blob_t;

struct r2t_session {
	uint64_t buffer_lba;
	uint32_t bytes_left;
	uint32_t bytes_done;
	blob_t   PDU_initiator;
        bool     is_write_same;  // receive 1 block, write 1 or more times
        bool     write_same_is_unmap;
};

typedef enum
{
	ir_as_is,  // only a sense and/or data pdu
	ir_empty_sense,  // only a possibly empty sense
	ir_r2t,  // R2T
} iscsi_reacion_t;

std::vector<std::string>      data_to_text_array(const uint8_t *const data, const size_t n);
std::pair<uint8_t *, size_t>  text_array_to_data(const std::vector<std::string> & in);
void                          set_bits(uint8_t *const target, const int bit_nr, const int length, const uint8_t value);
uint8_t                       get_bits(const uint8_t from, const int bit_nr, const int length);
std::pair<uint32_t, uint32_t> crc32_0x11EDC6F41(const uint8_t *data, const size_t len, std::optional<uint32_t> start_with);
