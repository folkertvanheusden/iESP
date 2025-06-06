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

backend_sdcard::backend_sdcard(const int led_read, const int led_write, const int pin_SD_MISO, const int pin_SD_MOSI, const int pin_SD_SCLK, const int pin_SD_CS, const std::optional<int> spi_speed, const std::string & disk_name):
	backend("SD-card"),
	led_read(led_read),
	led_write(led_write),
	pin_SD_MISO(pin_SD_MISO),
	pin_SD_MOSI(pin_SD_MOSI),
	pin_SD_SCLK(pin_SD_SCLK),
	pin_SD_CS(pin_SD_CS),
	spi_speed(spi_speed),
	disk_name(disk_name)
{
#if defined(RP2040W)
	mutex_init(&serial_access_lock);
#endif
}

bool backend_sdcard::begin()
{
	for(int i=0; i<3; i++) {
		if (reinit(i)) {
			DOLOG(logging::ll_info, "backend_sdcard::begin", "-", "Card serial: %s", get_serial().c_str());
			return true;
		}
	}

	DOLOG(logging::ll_info, "backend_sdcard::begin", "-", "SD-card init failed");

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
		DOLOG(logging::ll_info, "backend_sdcard::reinit", "-", "SD-card re-init");
	}

#if defined(RP2040W)
	SPI.begin();
	bool ok = true;
#else
	bool ok = false;
	SPI.begin(pin_SD_SCLK, pin_SD_MISO, pin_SD_MOSI, pin_SD_CS);
#if defined(WT_ETH01)
	const int start_speed = 22;
	const int end_speed   = 14;
	const int steps       = 2;
#else
	const int start_speed = 50;
	const int end_speed   = 14;
	const int steps       = 4;
#endif
	if (spi_speed.has_value()) {
		ok = sd.begin(SdSpiConfig(pin_SD_CS, SHARED_SPI, SD_SCK_MHZ(spi_speed.value())));
		if (ok)
			DOLOG(logging::ll_info, "backend_sdcard::reinit", "-", "Accessing SD card at %d MHz", spi_speed.value());
	}
	else {
		for(int sp=start_speed; sp>=end_speed; sp -= steps) {
			DOLOG(logging::ll_debug, "backend_sdcard::reinit", "-", "Trying %d MHz...", sp);
			if (sd.begin(SdSpiConfig(pin_SD_CS, SHARED_SPI, SD_SCK_MHZ(sp)))) {
				ok = true;
				DOLOG(logging::ll_info, "backend_sdcard::reinit", "-", "Accessing SD card at %d MHz", sp);
				break;
			}
		}
	}

	if (ok == false) {
		DOLOG(logging::ll_warning, "backend_sdcard::reinit", "-", "SD-card mount failed (assuming CS is on pin %d)", pin_SD_CS);
		sd.initErrorPrint(&Serial);
		write_led(led_read,  LOW);
		write_led(led_write, LOW);
		return false;
	}
#endif

	sd.ls(LS_DATE | LS_SIZE);

retry:
	if (file.open(disk_name.c_str(), O_RDWR) == false)
	{
		DOLOG(logging::ll_error, "backend_sdcard::reinit", "-", "Cannot access \"%s\" on SD-card", disk_name.c_str());
		write_led(led_read,  LOW);
		write_led(led_write, LOW);
		return false;
	}

	// virtual sizes
	card_size   = file.fileSize();
	DOLOG(logging::ll_info, "backend_sdcard::reinit", "-", "Virtual disk size: %u MB", uint32_t(card_size / 1024 / 1024));

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
	bs.n_syncs++;

#if defined(RP2040W)
	mutex_enter_blocking(&serial_access_lock);
#else
	std::lock_guard<std::mutex> lck(serial_access_lock);
#endif

	auto start = get_micros();
	if (file.sync() == false)
		DOLOG(logging::ll_error, "backend_sdcard::sync", "-", "Cannot sync data to SD-card");
	auto end   = get_micros();

#if defined(RP2040W)
	mutex_exit(&serial_access_lock);
#endif
	write_led(led_write, LOW);

	bs.io_wait   += end-start;
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
	auto     start            = get_micros();

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
	bs.bytes_written += n_bytes_to_write;

	wait_for_card();
	size_t bytes_written = file.write(data, n_bytes_to_write);
	bool   rc            = bytes_written == n_bytes_to_write;
	auto   end           = get_micros();
	if (!rc)
		DOLOG(logging::ll_warning, "backend_sdcard::write", "-", "Wrote %u bytes instead of %u", uint32_t(bytes_written), uint32_t(n_bytes_to_write));

#if defined(RP2040W)
	mutex_exit(&serial_access_lock);
#endif

	write_led(led_write, LOW);

	bs.io_wait   += end-start;
	bs.n_writes++;
	ts_last_acces = end;

	return rc;
}

bool backend_sdcard::trim(const uint64_t block_nr, const uint32_t n_blocks)
{
	bool     rc    = true;
	uint8_t *data  = new uint8_t[get_block_size()]();
	for(uint32_t i=0; i<n_blocks; i++) {
		if (write(block_nr + i, 1, data) == false) {
			DOLOG(logging::ll_error, "backend_sdcard::trim", "-", "Cannot trim");
			rc = false;
			break;
		}
	}
	delete [] data;
	// stats are updated in write()
	return rc;
}

bool backend_sdcard::read(const uint64_t block_nr, const uint32_t n_blocks, uint8_t *const data)
{
	write_led(led_read, HIGH);
	// Serial.printf("Read from block %" PRIu64 ", %u blocks\r\n", size_t(block_nr), n_blocks);

	uint64_t iscsi_block_size = get_block_size();
	uint64_t byte_address     = block_nr * iscsi_block_size;  // iSCSI to bytes
	uint64_t start            = get_micros();

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
	bs.bytes_read += n_bytes_to_read;

	size_t bytes_read = file.read(data, n_bytes_to_read);
	bool   rc         = bytes_read == n_bytes_to_read;
	auto   end        = get_micros();
	if (!rc)
		DOLOG(logging::ll_warning, "backend_sdcard::write", "-", "Read %u bytes instead of %u", uint32_t(bytes_read), uint32_t(n_bytes_to_read));

#if defined(RP2040W)
	mutex_exit(&serial_access_lock);
#endif
	write_led(led_read, LOW);
	bs.io_wait   += end-start;
	bs.n_reads++;
	ts_last_acces = end;
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
	auto              start  = get_micros();

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
		bs.bytes_read += block_size;

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

		bs.bytes_written += block_size;

		ts_last_acces = get_micros();
	}

	auto end    = get_micros();
	bs.io_wait += end-start;
	bs.n_reads++;
	bs.n_writes++;

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
