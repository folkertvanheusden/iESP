#include <optional>
#include <unistd.h>

#include "backend-sdcard-teensy41.h"
#include "gen.h"
#include "log.h"
#include "utils.h"


extern void write_led(const int gpio, const int state);

backend_sdcard_teensy41::backend_sdcard_teensy41(const int led_read, const int led_write):
	backend("SD-card"),
	led_read(led_read),
	led_write(led_write)
{
}

bool backend_sdcard_teensy41::begin()
{
	write_led(led_read,  HIGH);
	write_led(led_write, HIGH);

	Serial.println(F("Init SD-card backend..."));

	if (SD.sdfs.begin(SdioConfig(DMA_SDIO)))
		Serial.println(F("Init SD-card succeeded"));
	else {
		Serial.println(F("Init SD-card failed!"));
		write_led(led_read,  LOW);
		write_led(led_write, LOW);
		return false;
	}

	Serial.printf("Card serial: %s\r\n", get_serial().c_str());

	SD.sdfs.ls(LS_DATE | LS_SIZE);

	file = SD.sdfs.open(FILENAME, O_RDWR);
	if (!file)
	{
		DOLOG(logging::ll_error, "backend_sdcard_teensy41::begin", "-", "Cannot access " FILENAME " on SD-card");
		write_led(led_read,  LOW);
		write_led(led_write, LOW);
		return false;
	}

	// virtual sizes
	card_size   = file.fileSize();
	Serial.printf("Virtual disk size: %" PRIu64 "MB\r\n", uint64_t(card_size / 1024 / 1024));

	write_led(led_read,  LOW);
	write_led(led_write, LOW);

	return true;
}

backend_sdcard_teensy41::~backend_sdcard_teensy41()
{
	file.close();
	SD.sdfs.end();
}

bool backend_sdcard_teensy41::sync()
{
	write_led(led_write, HIGH);
	auto start = get_micros();
	if (file.sync() == false)
		DOLOG(logging::ll_error, "backend_sdcard_teensy41::sync", "-", "Cannot sync data to SD-card");
	auto end   = get_micros();
	if (file.sync() == false)
	write_led(led_write, LOW);

	bs.io_wait   += end-start;
	ts_last_acces = end;

	return true;
}

uint64_t backend_sdcard_teensy41::get_size_in_blocks() const
{
	return card_size / get_block_size();
}

uint64_t backend_sdcard_teensy41::get_block_size() const
{
	return 512;
}

bool backend_sdcard_teensy41::write(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data)
{
	// Serial.printf("Write to block %" PRIu64 ", %u blocks\r\n", block_nr, n_blocks);
	write_led(led_write, HIGH);

	uint64_t iscsi_block_size = get_block_size();
	uint64_t byte_address     = block_nr * iscsi_block_size;  // iSCSI to bytes
	auto     start            = get_micros();

	if (file.seekSet(byte_address) == false) {
		DOLOG(logging::ll_error, "backend_sdcard_teensy41::write", "-", "Cannot seek to %" PRIu64, byte_address);
		write_led(led_write, LOW);
		return false;
	}

	size_t n_bytes_to_write = n_blocks * iscsi_block_size;
	bs.bytes_written += n_bytes_to_write;

	bool rc = false;
	for(int i=0; i<5; i++) {  // 5 is arbitrarily chosen
		wait_for_card();
		arm_dcache_flush(data, n_bytes_to_write);
		size_t bytes_written = file.write(data, n_bytes_to_write);
		rc = bytes_written == n_bytes_to_write;
		if (rc)
			break;
		Serial.printf("Wrote %zu bytes instead of %zu\r\n", bytes_written, n_bytes_to_write);
		delay((i + 1) * 100); // 100ms is arbitrarily chosen
		Serial.printf("Retrying write of %" PRIu32 " blocks starting at block number % " PRIu64 "\r\n", n_blocks, block_nr);
	}
	if (!rc)
		DOLOG(logging::ll_error, "backend_sdcard_teensy41::write", "-", "Cannot write: %d", file.getError());

	write_led(led_write, LOW);

	auto end      = get_micros();
	bs.io_wait   += end-start;
	ts_last_acces = end;

	return rc;
}

bool backend_sdcard_teensy41::trim(const uint64_t block_nr, const uint32_t n_blocks)
{
	bool     rc    = true;
	uint8_t *data  = new uint8_t[get_block_size()]();
	auto     start = get_micros();
	for(uint32_t i=0; i<n_blocks; i++) {
		if (write(block_nr + i, 1, data) == false) {
			DOLOG(logging::ll_error, "backend_sdcard_teensy41::trim", "-", "Cannot trim");
			rc = false;
			break;
		}
	}
	delete [] data;
	auto end      = get_micros();
	bs.io_wait   += end-start;
	ts_last_acces = end;
	return rc;
}

bool backend_sdcard_teensy41::read(const uint64_t block_nr, const uint32_t n_blocks, uint8_t *const data)
{
	write_led(led_read, HIGH);
	// Serial.printf("Read from block %" PRIu64 ", %u blocks\r\n", block_nr, n_blocks);

	uint64_t iscsi_block_size = get_block_size();
	uint64_t byte_address     = block_nr * iscsi_block_size;  // iSCSI to bytes
	auto     start            = get_micros();

	if (file.seekSet(byte_address) == false) {
		DOLOG(logging::ll_error, "backend_sdcard_teensy41::read", "-", "Cannot seek to %" PRIu64, byte_address);
		write_led(led_read, LOW);
		return false;
	}

	size_t n_bytes_to_read = n_blocks * iscsi_block_size;
	bs.bytes_read += n_bytes_to_read;

	bool rc = false;
	for(int i=0; i<5; i++) {  // 5 is arbitrarily chosen
		arm_dcache_flush_delete(data, n_bytes_to_read);
		size_t bytes_read = file.read(data, n_bytes_to_read);
		arm_dcache_delete(data, n_bytes_to_read);
		rc = bytes_read == n_bytes_to_read;
		if (rc)
			break;
		DOLOG(logging::ll_error, "backend_sdcard_teensy41::read", "-", "Read %zu bytes instead of %zu", bytes_read, n_bytes_to_read);
		delay((i + 1) * 100); // 100ms is arbitrarily chosen
		DOLOG(logging::ll_error, "backend_sdcard_teensy41::read", "-", "Retrying read of %" PRIu32 " blocks starting at block number % " PRIu64, n_blocks, block_nr);
	}
	auto end = get_micros();
	if (!rc)
		DOLOG(logging::ll_error, "backend_sdcard_teensy41::read", "-", "Cannot read: %d", file.getError());
	write_led(led_read, LOW);
	bs.io_wait   += end-start;
	ts_last_acces = end;
	return rc;
}

backend::cmpwrite_result_t backend_sdcard_teensy41::cmpwrite(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data_write, const uint8_t *const data_compare)
{
	write_led(led_read,  HIGH);
	write_led(led_write, HIGH);

	auto lock_list  = lock_range(block_nr, n_blocks);
	auto block_size = get_block_size();

	cmpwrite_result_t result = cmpwrite_result_t::CWR_OK;
	uint8_t          *buffer = new uint8_t[block_size]();
	auto              start  = get_micros();

	// DO
	for(uint32_t i=0; i<n_blocks; i++) {
		uint64_t  offset = (block_nr + i) * block_size;

		if (file.seekSet(offset) == false) {
			DOLOG(logging::ll_error, "backend_sdcard_teensy41::cmpwrite", "-", "Cannot seek to %" PRIu64 " (read)", offset);
			result = cmpwrite_result_t::CWR_READ_ERROR;
			break;
		}

		// read
		arm_dcache_flush_delete(buffer, block_size);
		ssize_t rc     = file.read(buffer, block_size);
		arm_dcache_delete(buffer, block_size);
		if (rc != block_size) {
			result = cmpwrite_result_t::CWR_READ_ERROR;
			DOLOG(logging::ll_error, "backend_sdcard_teensy41::cmpwrite", "-", "Cannot read: %d", file.getError());
			break;
		}
		bs.bytes_read += block_size;

		// compare
		if (memcmp(buffer, &data_compare[i * block_size], block_size) != 0) {
			result = cmpwrite_result_t::CWR_MISMATCH;
			break;
		}

		// write
		if (file.seekSet(offset) == false) {
			DOLOG(logging::ll_error, "backend_sdcard_teensy41::cmpwrite", "-", "Cannot seek to %" PRIu64 " (write)", offset);
			result = cmpwrite_result_t::CWR_READ_ERROR;
			break;
		}

		wait_for_card();
		arm_dcache_flush(&data_write[i * block_size], block_size);
		ssize_t rc2 = file.write(&data_write[i * block_size], block_size);
		if (rc2 != block_size) {
			result = cmpwrite_result_t::CWR_WRITE_ERROR;
			DOLOG(logging::ll_error, "backend_sdcard_teensy41::cmpwrite", "-", "Cannot write: %d", file.getError());
			break;
		}

		bs.bytes_written += block_size;

		ts_last_acces = get_micros();
	}
	auto end    = get_micros();
	bs.io_wait += end-start;

	delete [] buffer;

	unlock_range(lock_list);

	write_led(led_read,  LOW);
	write_led(led_write, LOW);

	return result;
}

void backend_sdcard_teensy41::wait_for_card()
{
	while(file.isBusy())
		yield();
}

std::string backend_sdcard_teensy41::get_serial() const
{
	cid_t cid { };
	if (!SD.sdfs.card()->readCID(&cid))
		return DEFAULT_SERIAL;

	return myformat("%08x", cid.psn);
}
