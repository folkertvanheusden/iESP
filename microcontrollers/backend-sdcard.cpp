#include <fcntl.h>
#include <mutex>
#include <optional>
#include <unistd.h>
#include <sys/stat.h>

#include "backend-sdcard.h"
#include "gen.h"
#include "log.h"
#include "utils.h"


#ifdef RP2040W
#define SD_CS 17
#define SDCARD_SPI SPI1
#endif

extern void write_led(const int gpio, const int state);

backend_sdcard::backend_sdcard(const int led_read, const int led_write, const int pin_SD_MISO, const int pin_SD_MOSI, const int pin_SD_SCLK, const int pin_SD_CS):
	backend("SD-card"),
	led_read(led_read),
	led_write(led_write),
	pin_SD_MISO(pin_SD_MISO),
	pin_SD_MOSI(pin_SD_MOSI),
	pin_SD_SCLK(pin_SD_SCLK),
	pin_SD_CS(pin_SD_CS)
{
#if defined(RP2040W)
	mutex_init(&serial_access_lock);
#endif
}

bool backend_sdcard::begin()
{
	for(int i=0; i<3; i++) {
		if (reinit(i))
			return true;
	}

	Serial.println(F("SD-card init failed"));

	return false;
}

bool backend_sdcard::reinit(const bool close_first)
{
	std::lock_guard<std::mutex> lck(serial_access_lock);

	write_led(led_read,  HIGH);
	write_led(led_write, HIGH);
	if (close_first) {
		file.close();
		sd.end();
		Serial.println(F("Re-init SD-card backend..."));
	}

#if defined(RP2040W)
	SPI.begin();
	bool ok = true;
#else
	bool ok = false;
	SPI.begin(pin_SD_SCLK, pin_SD_MISO, pin_SD_MOSI, pin_SD_CS);
#if defined(WT_ETH01)
	for(int sp=22; sp>=14; sp -= 2) {
		Serial.printf("Trying %d MHz...\r\n", sp);
		if (sd.begin(SdSpiConfig(pin_SD_CS, SHARED_SPI, SD_SCK_MHZ(sp)))) {
			ok = true;
			Serial.printf("Accessing SD card at %d MHz\r\n", sp);
			break;
		}
	}
#else
	for(int sp=50; sp>=14; sp -= 4) {
		Serial.printf("Trying %d MHz...\r\n", sp);
		if (sd.begin(SdSpiConfig(pin_SD_CS, SHARED_SPI, SD_SCK_MHZ(sp)))) {
			ok = true;
			Serial.printf("Accessing SD card at %d MHz\r\n", sp);
			break;
		}
	}
#endif

	if (ok == false) {
		Serial.printf("SD-card mount failed (assuming CS is on pin %d)\r\n", pin_SD_CS);
		sd.initErrorPrint(&Serial);
		write_led(led_read,  LOW);
		write_led(led_write, LOW);
		return false;
	}
#endif

	sd.ls(LS_DATE | LS_SIZE);

retry:
	if (file.open(FILENAME, O_RDWR) == false)
	{
		DOLOG(logging::ll_error, "backend_sdcard::reinit", "-", "Cannot access " FILENAME " on SD-card");
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

backend_sdcard::~backend_sdcard()
{
	file.close();
	sd.end();
}

bool backend_sdcard::sync()
{
	write_led(led_write, HIGH);

	n_syncs++;

#if defined(RP2040W)
	mutex_enter_blocking(&serial_access_lock);
#else
	std::lock_guard<std::mutex> lck(serial_access_lock);
#endif

	if (file.sync() == false)
		DOLOG(logging::ll_error, "backend_sdcard::sync", "-", "Cannot sync data to SD-card");

#if defined(RP2040W)
	mutex_exit(&serial_access_lock);
#endif

	write_led(led_write, LOW);

	ts_last_acces = get_micros();

	return true;
}

uint64_t backend_sdcard::get_size_in_blocks() const
{
	return card_size / get_block_size();
}

uint64_t backend_sdcard::get_block_size() const
{
	return 512;
}

bool backend_sdcard::write(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data)
{
//	DOLOG(logging::ll_debug, "backend_sdcard::write", "-", "Write to block %" PRIu64 ", %u blocks\r\n", block_nr, n_blocks);
	write_led(led_write, HIGH);

	uint64_t iscsi_block_size = get_block_size();
	uint64_t byte_address     = block_nr * iscsi_block_size;  // iSCSI to bytes

#if defined(RP2040W)
	mutex_enter_blocking(&serial_access_lock);
#else
	std::lock_guard<std::mutex> lck(serial_access_lock);
#endif

	if (file.seekSet(byte_address) == false) {
		DOLOG(logging::ll_error, "backend_sdcard::write", "-", "Cannot seek to position");
		write_led(led_write, LOW);
#if defined(RP2040W)
		mutex_exit(&serial_access_lock);
#endif
		return false;
	}

	size_t n_bytes_to_write = n_blocks * iscsi_block_size;
	bytes_written += n_bytes_to_write;

	wait_for_card();
	size_t bytes_written = file.write(data, n_bytes_to_write);
	bool rc = bytes_written == n_bytes_to_write;
	if (!rc)
		Serial.printf("Wrote %zu bytes instead of %zu\r\n", bytes_written, n_bytes_to_write);

#if defined(RP2040W)
	mutex_exit(&serial_access_lock);
#endif

	write_led(led_write, LOW);

	ts_last_acces = get_micros();

	return rc;
}

bool backend_sdcard::trim(const uint64_t block_nr, const uint32_t n_blocks)
{
	bool rc = true;
	uint8_t *data = new uint8_t[get_block_size()]();
	for(uint32_t i=0; i<n_blocks; i++) {
		if (write(block_nr + i, 1, data) == false) {
			DOLOG(logging::ll_error, "backend_sdcard::trim", "-", "Cannot trim");
			rc = false;
			break;
		}
	}
	delete [] data;
	ts_last_acces = get_micros();
	n_trims += n_blocks;
	return rc;
}

bool backend_sdcard::read(const uint64_t block_nr, const uint32_t n_blocks, uint8_t *const data)
{
	write_led(led_read, HIGH);
	// Serial.printf("Read from block %" PRIu64 ", %u blocks\r\n", size_t(block_nr), n_blocks);

	uint64_t iscsi_block_size = get_block_size();
	uint64_t byte_address     = block_nr * iscsi_block_size;  // iSCSI to bytes

#if defined(RP2040W)
	mutex_enter_blocking(&serial_access_lock);
#else
	std::lock_guard<std::mutex> lck(serial_access_lock);
#endif

	if (file.seekSet(byte_address) == false) {
		DOLOG(logging::ll_error, "backend_sdcard::read", "-", "Cannot seek to position");
		write_led(led_read, LOW);
#if defined(RP2040W)
		mutex_exit(&serial_access_lock);
#endif
		return false;
	}

	size_t n_bytes_to_read = n_blocks * iscsi_block_size;
	bytes_read += n_bytes_to_read;

	bool rc = false;
	size_t bytes_read = file.read(data, n_bytes_to_read);
	rc = bytes_read == n_bytes_to_read;
	if (!rc)
		Serial.printf("Read %zu bytes instead of %zu\r\n", bytes_read, n_bytes_to_read);

#if defined(RP2040W)
	mutex_exit(&serial_access_lock);
#endif
	write_led(led_read, LOW);
	ts_last_acces = get_micros();
	return rc;
}

backend::cmpwrite_result_t backend_sdcard::cmpwrite(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data_write, const uint8_t *const data_compare)
{
	write_led(led_read,  HIGH);
	write_led(led_write, HIGH);

#if defined(RP2040W)
	mutex_enter_blocking(&serial_access_lock);
#else
	auto lock_list  = lock_range(block_nr, n_blocks);
#endif
	auto block_size = get_block_size();

	cmpwrite_result_t result = cmpwrite_result_t::CWR_OK;
	uint8_t          *buffer = new uint8_t[block_size]();

	// DO
	for(uint32_t i=0; i<n_blocks; i++) {
		uint64_t  offset = (block_nr + i) * block_size;

		if (file.seekSet(offset) == false) {
			DOLOG(logging::ll_error, "backend_sdcard::cmpwrite", "-", "Cannot seek to position (read)");
			result = cmpwrite_result_t::CWR_READ_ERROR;
			break;
		}

		// read
		ssize_t rc     = file.read(buffer, block_size);
		if (rc != block_size) {
			result = cmpwrite_result_t::CWR_READ_ERROR;
			break;
		}
		bytes_read += block_size;

		// compare
		if (memcmp(buffer, &data_compare[i * block_size], block_size) != 0) {
			result = cmpwrite_result_t::CWR_MISMATCH;
			break;
		}

		// write
		if (file.seekSet(offset) == false) {
			DOLOG(logging::ll_error, "backend_sdcard::cmpwrite", "-", "Cannot seek to position (write)");
			result = cmpwrite_result_t::CWR_READ_ERROR;
			break;
		}

		wait_for_card();
		ssize_t rc2 = file.write(&data_write[i * block_size], block_size);
		if (rc2 != block_size) {
			result = cmpwrite_result_t::CWR_WRITE_ERROR;
			break;
		}

		bytes_written += block_size;

		ts_last_acces = get_micros();
	}

	delete [] buffer;

#if defined(RP2040W)
	mutex_exit(&serial_access_lock);
#else
	unlock_range(lock_list);
#endif

	write_led(led_read,  LOW);
	write_led(led_write, LOW);

	return result;
}

void backend_sdcard::wait_for_card()
{
	while(sd.card()->isBusy())
		yield();
}

std::string backend_sdcard::get_serial() const
{
	cid_t cid { };
	if (!sd.card()->readCID(&cid))
		return DEFAULT_SERIAL;

	return myformat("%08x", cid.psn());
}
