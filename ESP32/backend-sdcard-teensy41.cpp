#include <optional>
#include <unistd.h>

#include "backend-sdcard-teensy41.h"
#include "log.h"
#include "utils.h"

#define FILENAME "test.dat"

extern void write_led(const int gpio, const int state);

backend_sdcard_teensy41::backend_sdcard_teensy41(const int led_read, const int led_write) :
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

	SD.sdfs.ls(LS_DATE | LS_SIZE);

	file = SD.sdfs.open(FILENAME, O_RDWR);
	if (!file)
	{
		errlog("Cannot access test.dat on SD-card");
		write_led(led_read,  LOW);
		write_led(led_write, LOW);
		return false;
	}

	// virtual sizes
	card_size   = file.fileSize();
	sector_size = 512;
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

	n_syncs++;

	if (file.sync() == false)
		errlog("SD card backend: sync failed");

	write_led(led_write, LOW);

	ts_last_acces = get_micros();

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
	// Serial.printf("Write to block %zu, %u blocks\r\n", size_t(block_nr), n_blocks);
	write_led(led_write, HIGH);

	uint64_t iscsi_block_size = get_block_size();
	uint64_t byte_address     = block_nr * iscsi_block_size;  // iSCSI to bytes

	if (file.seekSet(byte_address) == false) {
		errlog("Cannot seek to position");
		write_led(led_write, LOW);
		return false;
	}

	size_t n_bytes_to_write = n_blocks * iscsi_block_size;
	bytes_written += n_bytes_to_write;

	bool rc = false;
	for(int i=0; i<5; i++) {  // 5 is arbitrarily chosen
		size_t bytes_written = file.write(data, n_bytes_to_write);
		rc = bytes_written == n_bytes_to_write;
		if (rc)
			break;
		Serial.printf("Wrote %zu bytes instead of %zu\r\n", bytes_written, n_bytes_to_write);
		delay((i + 1) * 100); // 100ms is arbitrarily chosen
		Serial.printf("Retrying write of %" PRIu32 " blocks starting at block number % " PRIu64 "\r\n", n_blocks, block_nr);
	}
	if (!rc)
		errlog("Cannot write (%d)", file.getError());

	write_led(led_write, LOW);

	ts_last_acces = get_micros();

	return rc;
}

bool backend_sdcard_teensy41::trim(const uint64_t block_nr, const uint32_t n_blocks)
{
	bool rc = true;
	uint8_t *data = new uint8_t[get_block_size()];
	for(uint32_t i=0; i<n_blocks; i++) {
		if (write(block_nr + i, 1, data) == false) {
			errlog("Cannot \"trim\"");
			rc = false;
			break;
		}
	}
	delete [] data;
	ts_last_acces = get_micros();
	n_trims++;
	return rc;
}

bool backend_sdcard_teensy41::read(const uint64_t block_nr, const uint32_t n_blocks, uint8_t *const data)
{
	write_led(led_read, HIGH);
	// Serial.printf("Read from block %zu, %u blocks\r\n", size_t(block_nr), n_blocks);

	uint64_t iscsi_block_size = get_block_size();
	uint64_t byte_address     = block_nr * iscsi_block_size;  // iSCSI to bytes

	if (file.seekSet(byte_address) == false) {
		errlog("Cannot seek to position");
		write_led(led_read, LOW);
		return false;
	}

	size_t n_bytes_to_read = n_blocks * iscsi_block_size;
	bytes_read += n_bytes_to_read;

	bool rc = false;
	for(int i=0; i<5; i++) {  // 5 is arbitrarily chosen
		size_t bytes_read = file.read(data, n_bytes_to_read);
		rc = bytes_read == n_bytes_to_read;
		if (rc)
			break;
		Serial.printf("Read %zu bytes instead of %zu\r\n", bytes_read, n_bytes_to_read);
		delay((i + 1) * 100); // 100ms is arbitrarily chosen
		Serial.printf("Retrying read of %" PRIu32 " blocks starting at block number % " PRIu64 "\r\n", n_blocks, block_nr);
	}
ok:
	if (!rc)
		errlog("Cannot read (%d)", file.getError());
	write_led(led_read, LOW);
	ts_last_acces = get_micros();
	return rc;
}
