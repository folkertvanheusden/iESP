#pragma once
#include <cstddef>
#include <cstdint>
#if !defined(TEENSY4_1)
#include <mutex>
#endif
#include <thread>
#include <utility>

#include "backend.h"
#include "gen.h"
#include "iscsi.h"
#include "iscsi-pdu.h"


struct scsi_response
{
	iscsi_reacion_t              type;
	std::vector<uint8_t>         sense_data;  // error data
	bool                         data_is_meta;  // scsi command reply data
	std::optional<uint64_t>      amount_of_data_expected;  // needed for iSCSI to calculate residual count

	struct {
		bool is_inline;  // if true, then next is valid

		struct {
			std::pair<uint8_t *, size_t> data;  // meta data or disk-data

			// if not inline
			data_descriptor location;
		} what;
	} io;

	r2t_session r2t;

	scsi_response(const iscsi_reacion_t type_in) : type(type_in) {
		data_is_meta = true;
		io.is_inline = false;
		io.what.data = { };
		io.what.location = { };
		r2t = { };
	}
};

typedef enum { cs_6, cs_10, cs_16 } cdb_size_t;

void get_lba_len(const uint8_t *const CDB, const cdb_size_t s, uint64_t *const lba, uint32_t *const transfer_length);

class scsi
{
private:
	backend    *const b          { nullptr };
	const int         trim_level { 1       };
	std::string       serial;
#if !defined(ARDUINO) && !defined(NDEBUG)
	std::atomic_uint64_t cmd_use_count[256] { };
#endif

#if !(defined(TEENSY4_1) || defined(RP2040W))
	std::mutex locked_by_lock;
	std::optional<std::thread::id> locked_by;
#endif

	scsi_response test_unit_ready(const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data);
	scsi_response mode_sense_6(const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data);
	scsi_response inquiry(const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data);
	scsi_response read_capacity_10(const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data);
	scsi_response get_lba_status(const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data);
	scsi_response write_verify(io_stats_t *const is, const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data, const uint8_t opcode);
	scsi_response read_(const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data, const uint8_t opcode);
	scsi_response sync_cache(io_stats_t *const is, const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data);
	scsi_response report_luns(const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data);
	scsi_response report_supported_operation_codes(const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data);
	scsi_response compare_and_write(io_stats_t *const is, const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data);
	scsi_response prefetch(const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data, const uint8_t opcode);
	scsi_response reserve(const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data);
	scsi_response release(const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data);
	scsi_response unmap(io_stats_t *const is, const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data);
	scsi_response write_same(io_stats_t *const is, const std::string & identifier, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data, const uint8_t opcode);

	std::optional<std::vector<uint8_t> > validate_request(const uint64_t lba, const uint32_t n_blocks, const uint8_t *const CDB) const;
	std::optional<std::vector<uint8_t> > validate_request(const uint64_t lba) const;

public:
	scsi(backend *const b, const int trim_level);
	virtual ~scsi();

	enum scsi_opcode {
		o_test_unit_ready  = 0x00,
		o_request_sense    = 0x02,
		o_read_6           = 0x08,
		o_write_6          = 0x0a,
		o_seek             = 0x0b,
		o_inquiry          = 0x12,
		o_reserve_6        = 0x16,
		o_release_6        = 0x17,
		o_mode_sense_6     = 0x1a,
		o_read_capacity_10 = 0x25,
		o_read_10          = 0x28,
		o_write_10         = 0x2a,
		o_write_verify_10  = 0x2e,
		o_prefetch_10      = 0x34,
		o_sync_cache_10    = 0x35,
		o_write_same_10    = 0x41,
		o_unmap            = 0x42,
		o_read_16          = 0x88,
		o_compare_and_write= 0x89,
		o_write_16         = 0x8a,
		o_prefetch_16      = 0x90,
		o_write_same_16    = 0x93,
		o_get_lba_status   = 0x9e,
		o_report_luns      = 0xa0,
		o_rep_sup_oper     = 0xa3,
	};

	enum scsi_lock_status {
		l_not_locked,
		l_locked,  // locked by caller
		l_locked_other,  // locked by someone else
	};

	enum scsi_rw_result {
		rw_ok,
		rw_fail_rw,
		rw_fail_locked,
		rw_fail_general,
		rw_fail_mismatch
	};

	uint64_t get_size_in_blocks() const;
	uint64_t get_block_size()     const;

	scsi_lock_status reserve_device();
	bool             unlock_device();
	scsi_lock_status locking_status();

	scsi_rw_result sync    (io_stats_t *const is);
	scsi_rw_result write   (io_stats_t *const is, const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data);
	scsi_rw_result trim    (io_stats_t *const is, const uint64_t block_nr, const uint32_t n_blocks);
	scsi_rw_result read    (io_stats_t *const is, const uint64_t block_nr, const uint32_t n_blocks,       uint8_t *const data);
	scsi_rw_result cmpwrite(io_stats_t *const is, const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const write_data, const uint8_t *const compare_data);

	std::optional<scsi_response> send(io_stats_t *const is, const uint64_t lun, const uint8_t *const CDB, const size_t size, std::pair<uint8_t *, size_t> data);

	std::vector<uint8_t> error_reservation_conflict_1()  const;
	std::vector<uint8_t> error_reservation_conflict_2()  const;
	std::vector<uint8_t> error_not_implemented()         const;
	std::vector<uint8_t> error_read_error()              const;
	std::vector<uint8_t> error_write_error()             const;
	std::vector<uint8_t> error_compare_and_write_count() const;
	std::vector<uint8_t> error_out_of_range()            const;
	std::vector<uint8_t> error_miscompare()              const;
	std::vector<uint8_t> error_invalid_field()           const;
};
