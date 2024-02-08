#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "backend-sdcard.h"
#include "log.h"

#ifdef RP2040W
#define CS_SD 17
#define SDCARD_SPI SPI1
#else
#define CS_SD 5
#endif
#define FILENAME "test.dat"

backend_sdcard::backend_sdcard()
{
	Serial.println(F("Init SD-card backend..."));

	bool ok = false;
	for(int sp=50; sp>=14; sp -= 4) {
		Serial.printf("Trying %d MHz...\r\n", sp);
		if (sd.begin(CS_SD, SD_SCK_MHZ(sp))) {
			ok = true;
			Serial.printf("Accessing SD card at %d MHz\r\n", sp);
			break;
		}
	}

	if (ok == false) {
		Serial.printf("SD-card mount failed (assuming CS is on pin %d)", CS_SD);
		sd.initErrorHalt(&Serial);
		return;
	}

	sd.ls();

retry:
	if (sd.exists(FILENAME) == false)
		init_file();

	if (file.open(FILENAME, O_RDWR) == false) {
		Serial.println(F("Cannot access test.dat on SD-card"));
		return;
	}

	// virtual sizes
	card_size   = file.fileSize();
	sector_size = 512;

	if (card_size == 0) {
		Serial.println(F("File is 0 bytes, recreating in 5s..."));
		file.close();
		sd.remove(FILENAME);
		delay(5000);
		goto retry;
	}

	Serial.printf("Virtual disk size: %zuMB\r\n", size_t(card_size / 1024 / 1024));
}

backend_sdcard::~backend_sdcard()
{
	file.close();

	sd.end();
}

bool backend_sdcard::sync()
{
	n_syncs++;

	if (file.sync() == false)
		Serial.println(F("SD card backend: sync failed"));

	return true;
}

void backend_sdcard::init_file()
{
	Serial.println(F("Creating " FILENAME "..."));

	if (file.open(FILENAME, O_RDWR | O_CREAT) == false) {
		Serial.println(F("Cannot create backend file"));
		return;
	}

	uint64_t vol_free = sd.vol()->freeClusterCount();
	Serial.printf("Free clusters: %zu\r\n", size_t(vol_free));

	uint64_t total_free_bytes = vol_free * sd.vol()->sectorsPerCluster() * 512ll;
	Serial.printf("Free space in bytes: %zu\r\n", size_t(total_free_bytes));

	total_free_bytes = (total_free_bytes * 7 / 8) & ~511;

	if (file.truncate(total_free_bytes) == false)
		Serial.printf("Cannot resize file to %zu bytes: %d\r\n", total_free_bytes, file.getError());

	file.close();

	file.ls();
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

	uint64_t iscsi_block_size = get_block_size();
	uint64_t byte_address     = block_nr * iscsi_block_size;  // iSCSI to bytes

	if (file.seekSet(byte_address) == false) {
		Serial.println(F("Cannot seek to position"));
		return false;
	}

	size_t n_bytes_to_write = n_blocks * iscsi_block_size;
	bytes_written += n_bytes_to_write;

	bool rc = file.write(data, n_bytes_to_write) == n_bytes_to_write;
	if (!rc)
		Serial.printf("Cannot write (%d)\r\n", file.getError());
	return rc;
}

bool backend_sdcard::read(const uint64_t block_nr, const uint32_t n_blocks, uint8_t *const data)
{
	// Serial.printf("Read from block %zu, %u blocks\r\n", size_t(block_nr), n_blocks);

	uint64_t iscsi_block_size = get_block_size();
	uint64_t byte_address     = block_nr * iscsi_block_size;  // iSCSI to bytes

	if (file.seekSet(byte_address) == false) {
		Serial.println(F("Cannot seek to position"));
		return false;
	}

	size_t n_bytes_to_read = n_blocks * iscsi_block_size;
	bytes_read += n_bytes_to_read;

	bool rc = file.read(data, n_bytes_to_read) == n_bytes_to_read;
	if (!rc)
		Serial.printf("Cannot read (%d)\r\n", file.getError());
	return rc;
}
