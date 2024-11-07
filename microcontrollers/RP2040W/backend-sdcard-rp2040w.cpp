#include <Arduino.h>
#include <optional>
#include <unistd.h>

#include "backend-sdcard-rp2040w.h"
#include "gen.h"
#include "log.h"
#include "utils.h"

#define FILENAME "test.dat"

extern void write_led(const int gpio, const int state);

backend_sdcard_rp2040w::backend_sdcard_rp2040w(const int led_read, const int led_write):
	backend("SD-card"),
	led_read(led_read),
	led_write(led_write)
{
}

bool backend_sdcard_rp2040w::begin()
{
	write_led(led_read,  HIGH);
	write_led(led_write, HIGH);

///
	static spi_t spi = {
		.hw_inst = spi1,  // RP2040 SPI component
		.miso_gpio = 8,   // GPIO number (not Pico pin number)
		.mosi_gpio = 11,
		.sck_gpio = 10,
		.baud_rate = 12 * 1000 * 1000,  // Actual frequency: 10416666
		.set_drive_strength = true,
		.mosi_gpio_drive_strength = GPIO_DRIVE_STRENGTH_12MA,
		.sck_gpio_drive_strength = GPIO_DRIVE_STRENGTH_12MA
	};

	// Hardware Configuration of SPI Interface object:
	static sd_spi_if_t spi_if = {
		.spi = &spi,   // Pointer to the SPI driving this card
		.ss_gpio = 12  // The SPI slave select GPIO for this SD card
	};

	// Hardware Configuration of the SD Card object:
	static sd_card_t sd_card = {
		.type = SD_IF_SPI,
		.spi_if_p = &spi_if,  // Pointer to the SPI interface driving this card
				      // SD Card detect:
		.use_card_detect = false,
		.card_detect_gpio = 14,
		.card_detected_true = 0, // What the GPIO read returns when a card is present.
		.card_detect_use_pull = false,
		.card_detect_pull_hi = false
	};

	Serial.println(F(" - add card"));
	FatFsNs::SdCard* card_p(FatFsNs::FatFs::add_sd_card(&sd_card));

	// The H/W config must be set up before this is called:
	Serial.println(F(" - sd_init_driver"));
	sd_init_driver(); 

	Serial.println(F(" - mount"));
	card_p->mount();

	FRESULT frc = file.open(FILENAME, FA_OPEN_APPEND | FA_WRITE | FA_READ);
	if (frc != FR_OK)
	{
		DOLOG(logging::ll_error, "backend_sdcard_rp2040w::begin", "-", "Cannot access " FILENAME " on SD-card");
		write_led(led_read,  LOW);
		write_led(led_write, LOW);
		return false;
	}

	// virtual sizes
	card_size   = file.size();
	sector_size = 512;
	Serial.printf("Virtual disk size: %" PRIu64 "MB\r\n", uint64_t(card_size / 1024 / 1024));

	write_led(led_read,  LOW);
	write_led(led_write, LOW);

	return true;
}

backend_sdcard_rp2040w::~backend_sdcard_rp2040w()
{
	file.close();
}

bool backend_sdcard_rp2040w::sync()
{
	write_led(led_write, HIGH);
	if (file.sync() != FR_OK)
		DOLOG(logging::ll_error, "backend_sdcard_rp2040w::sync", "-", "Cannot sync data to SD-card");
	write_led(led_write, LOW);

	bs.n_syncs++;
	ts_last_acces = get_micros();

	return true;
}

uint64_t backend_sdcard_rp2040w::get_size_in_blocks() const
{
	return card_size / get_block_size();
}

uint64_t backend_sdcard_rp2040w::get_block_size() const
{
	return 512;
}

bool backend_sdcard_rp2040w::write(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data)
{
	// Serial.printf("Write to block %zu, %u blocks\r\n", size_t(block_nr), n_blocks);
	write_led(led_write, HIGH);

	uint64_t iscsi_block_size = get_block_size();
	uint64_t byte_address     = block_nr * iscsi_block_size;  // iSCSI to bytes

	if (file.lseek(byte_address) != FR_OK) {
		DOLOG(logging::ll_error, "backend_sdcard_rp2040w::write", "-", "Cannot seek to %" PRIu64, byte_address);
		write_led(led_write, LOW);
		return false;
	}

	size_t n_bytes_to_write = n_blocks * iscsi_block_size;
	bs.bytes_written += n_bytes_to_write;

	bool rc = false;
	for(int i=0; i<5; i++) {  // 5 is arbitrarily chosen
		UINT bytes_written = 0;
		file.write(data, n_bytes_to_write, &bytes_written);
		rc = bytes_written == n_bytes_to_write;
		if (rc)
			break;
		Serial.printf("Wrote %zu bytes instead of %zu\r\n", bytes_written, n_bytes_to_write);
		delay((i + 1) * 100); // 100ms is arbitrarily chosen
		Serial.printf("Retrying write of %" PRIu32 " blocks starting at block number % " PRIu64 "\r\n", n_blocks, block_nr);
	}
	if (!rc)
		DOLOG(logging::ll_error, "backend_sdcard_rp2040w::write", "-", "Cannot write: %d", file.error());

	write_led(led_write, LOW);

	ts_last_acces = get_micros();

	return rc;
}

bool backend_sdcard_rp2040w::trim(const uint64_t block_nr, const uint32_t n_blocks)
{
	bool rc = true;
	uint8_t *data = new uint8_t[get_block_size()];
	for(uint32_t i=0; i<n_blocks; i++) {
		if (write(block_nr + i, 1, data) == false) {
			DOLOG(logging::ll_error, "backend_sdcard_rp2040w::trim", "-", "Cannot trim");
			rc = false;
			break;
		}
	}
	delete [] data;
	ts_last_acces = get_micros();
	bs.n_trims++;
	return rc;
}

bool backend_sdcard_rp2040w::read(const uint64_t block_nr, const uint32_t n_blocks, uint8_t *const data)
{
	write_led(led_read, HIGH);
	// Serial.printf("Read from block %zu, %u blocks\r\n", size_t(block_nr), n_blocks);

	uint64_t iscsi_block_size = get_block_size();
	uint64_t byte_address     = block_nr * iscsi_block_size;  // iSCSI to bytes

	if (file.lseek(byte_address) != FR_OK) {
		DOLOG(logging::ll_error, "backend_sdcard_rp2040w::read", "-", "Cannot seek to %" PRIu64, byte_address);
		write_led(led_read, LOW);
		return false;
	}

	size_t n_bytes_to_read = n_blocks * iscsi_block_size;
	bs.bytes_read += n_bytes_to_read;

	bool rc = false;
	for(int i=0; i<5; i++) {  // 5 is arbitrarily chosen
		UINT bytes_read = 0;
		file.read(data, n_bytes_to_read, &bytes_read);
		rc = bytes_read == n_bytes_to_read;
		if (rc)
			break;
		DOLOG(logging::ll_error, "backend_sdcard_rp2040w::read", "-", "Read %zu bytes instead of %zu", bytes_read, n_bytes_to_read);
		delay((i + 1) * 100); // 100ms is arbitrarily chosen
		DOLOG(logging::ll_error, "backend_sdcard_rp2040w::read", "-", "Retrying read of %" PRIu32 " blocks starting at block number % " PRIu64, n_blocks, block_nr);
	}
	if (!rc)
		DOLOG(logging::ll_error, "backend_sdcard_rp2040w::read", "-", "Cannot read: %d", file.error());
	write_led(led_read, LOW);
	ts_last_acces = get_micros();
	return rc;
}

backend::cmpwrite_result_t backend_sdcard_rp2040w::cmpwrite(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data_write, const uint8_t *const data_compare)
{
	write_led(led_read, HIGH);

	auto lock_list  = lock_range(block_nr, n_blocks);
	auto block_size = get_block_size();

	cmpwrite_result_t result = cmpwrite_result_t::CWR_OK;
	uint8_t          *buffer = new uint8_t[block_size]();

	// DO
	for(uint32_t i=0; i<n_blocks; i++) {
		uint64_t  offset = (block_nr + i) * block_size;

		if (file.lseek(offset) != FR_OK) {
			DOLOG(logging::ll_error, "backend_sdcard_rp2040w::cmpwrite", "-", "Cannot seek to %" PRIu64 " (read)", offset);
			result = cmpwrite_result_t::CWR_READ_ERROR;
			break;
		}

		// read
		UINT    rc     = 0;
		file.read(buffer, block_size, &rc);
		if (rc != block_size) {
			result = cmpwrite_result_t::CWR_READ_ERROR;
			DOLOG(logging::ll_error, "backend_sdcard_rp2040w::cmpwrite", "-", "Cannot read: %d", file.error());
			break;
		}

		// compare
		if (memcmp(buffer, &data_compare[i * block_size], block_size) != 0) {
			result = cmpwrite_result_t::CWR_MISMATCH;
			break;
		}

		// write
		if (file.lseek(offset) != FR_OK) {
			DOLOG(logging::ll_error, "backend_sdcard_rp2040w::cmpwrite", "-", "Cannot seek to %" PRIu64 " (write)", offset);
			result = cmpwrite_result_t::CWR_READ_ERROR;
			break;
		}

		UINT    rc2 = 0;
		file.write(&data_write[i * block_size], block_size, &rc2);
		if (rc2 != block_size) {
			result = cmpwrite_result_t::CWR_WRITE_ERROR;
			DOLOG(logging::ll_error, "backend_sdcard_rp2040w::cmpwrite", "-", "Cannot write: %d", file.error());
			break;
		}

		ts_last_acces = get_micros();
	}

	delete [] buffer;

	unlock_range(lock_list);

	write_led(led_read, LOW);

	return result;
}

std::string backend_sdcard_rp2040w::get_serial() const
{
	return DEFAULT_SERIAL;
}
