#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "backend-sdcard.h"
#include "log.h"
#include "utils.h"

#ifdef RP2040W
#define SD_CS 17
#define SDCARD_SPI SPI1
#else
// Ethernet:
// 18 SCK
// 19 MISO
// 23 MOSI
// 5  CS
#endif

#define FILENAME "test.dat"

#define SD_MISO     2       // SD-Card, IO2
#define SD_MOSI    15       // SD-Card, TD0
#define SD_SCLK    14       // SD-Card, TMS
#define SD_CS      12       // SD-Card, TDI

extern void write_led(const int gpio, const int state);

backend_sdcard::backend_sdcard(const int led_read, const int led_write) :
	led_read(led_read),
	led_write(led_write)
{
}

bool backend_sdcard::begin()
{
	for(int i=0; i<3; i++) {
		if (reinit(i))
			return true;
	}

	return false;
}

bool backend_sdcard::reinit(const bool close_first)
{
	write_led(led_read,  HIGH);
	write_led(led_write, HIGH);
	if (close_first) {
		file.close();
		sd.end();
		Serial.println(F("Re-init SD-card backend..."));
	}
	else {
		Serial.println(F("Init SD-card backend..."));
	}

	SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);

	bool ok = false;
	for(int sp=50; sp>=14; sp -= 4) {
		Serial.printf("Trying %d MHz...\r\n", sp);
		if (sd.begin(SdSpiConfig(SD_CS, DEDICATED_SPI, SD_SCK_MHZ(sp)))) {
			ok = true;
			Serial.printf("Accessing SD card at %d MHz\r\n", sp);
			break;
		}
	}

	if (ok == false) {
		Serial.printf("SD-card mount failed (assuming CS is on pin %d)\r\n", SD_CS);
		sd.initErrorPrint(&Serial);
		write_led(led_read,  LOW);
		write_led(led_write, LOW);
		return false;
	}

	sd.ls(LS_DATE | LS_SIZE);

retry:
	if (file.open(FILENAME, O_RDWR) == false) {
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

backend_sdcard::~backend_sdcard()
{
	file.close();
	sd.end();
}

bool backend_sdcard::sync()
{
	write_led(led_write, HIGH);

	n_syncs++;

	std::lock_guard<std::mutex> lck(serial_access_lock);

	if (file.sync() == false)
		errlog("SD card backend: sync failed");

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
	// Serial.printf("Write to block %zu, %u blocks\r\n", size_t(block_nr), n_blocks);
	write_led(led_write, HIGH);

	uint64_t iscsi_block_size = get_block_size();
	uint64_t byte_address     = block_nr * iscsi_block_size;  // iSCSI to bytes

	std::lock_guard<std::mutex> lck(serial_access_lock);

	if (file.seekSet(byte_address) == false) {
		errlog("Cannot seek to position");
		write_led(led_write, LOW);
		return false;
	}

	size_t n_bytes_to_write = n_blocks * iscsi_block_size;
	bytes_written += n_bytes_to_write;

	bool rc = false;
	for(int k=2; k>=0; k--) {  // 2 is arbitrarily chosen
		for(int i=0; i<5; i++) {  // 5 is arbitrarily chosen
			rc = file.write(data, n_bytes_to_write) == n_bytes_to_write;
			if (rc)
				goto ok;
			delay((i + 1) * 100); // 100ms is arbitrarily chosen
			Serial.println(F("Retrying write"));
		}

		if (k)
			reinit(true);
	}
ok:
	if (!rc)
		errlog("Cannot write (%d)", file.getError());

	write_led(led_write, LOW);

	ts_last_acces = get_micros();

	return rc;
}

bool backend_sdcard::trim(const uint64_t block_nr, const uint32_t n_blocks)
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

bool backend_sdcard::read(const uint64_t block_nr, const uint32_t n_blocks, uint8_t *const data)
{
	write_led(led_read, HIGH);
	// Serial.printf("Read from block %zu, %u blocks\r\n", size_t(block_nr), n_blocks);

	uint64_t iscsi_block_size = get_block_size();
	uint64_t byte_address     = block_nr * iscsi_block_size;  // iSCSI to bytes

	std::lock_guard<std::mutex> lck(serial_access_lock);

	if (file.seekSet(byte_address) == false) {
		errlog("Cannot seek to position");
		write_led(led_read, LOW);
		return false;
	}

	size_t n_bytes_to_read = n_blocks * iscsi_block_size;
	bytes_read += n_bytes_to_read;

	bool rc = false;
	for(int k=2; k>=0; k--) {  // 2 is arbitrarily chosen
		for(int i=0; i<5; i++) {  // 5 is arbitrarily chosen
			rc = file.read(data, n_bytes_to_read) == n_bytes_to_read;
			if (rc)
				goto ok;
			delay((i + 1) * 100); // 100ms is arbitrarily chosen
			Serial.println(F("Retrying read"));
		}

		if (k)
			reinit(true);
	}
ok:
	if (!rc)
		errlog("Cannot read (%d)", file.getError());
	write_led(led_read, LOW);
	ts_last_acces = get_micros();
	return rc;
}
