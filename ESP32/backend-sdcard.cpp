#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "backend-sdcard.h"
#include "log.h"

#ifdef RP2040W
#define CS_SD 17
#define SDCARD_SPI SPI1
#else
// 18 SCK
// 19 MISO
// 23 MOSI
#define CS_SD 5
#define LED_GREEN 16
#define LED_RED   17
#endif
#define FILENAME "test.dat"

backend_sdcard::backend_sdcard()
{
#ifdef LED_GREEN
	pinMode(LED_GREEN, OUTPUT);
#endif
#ifdef LED_RED
	pinMode(LED_RED,   OUTPUT);
#endif

	bool close_first = false;

	while(!reinit(close_first))
		close_first = true;
}

bool backend_sdcard::reinit(const bool close_first)
{
	if (close_first) {
		file.close();
		sd.end();
		Serial.println(F("Re-init SD-card backend..."));
	}
	else {
		Serial.println(F("Init SD-card backend..."));
	}

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
		Serial.printf("SD-card mount failed (assuming CS is on pin %d)\r\n", CS_SD);
		sd.initErrorPrint(&Serial);
		return false;
	}

	sd.ls(LS_DATE | LS_SIZE);

retry:
	if (file.open(FILENAME, O_RDWR) == false) {
		Serial.println(F("Cannot access test.dat on SD-card"));
		return false;
	}

	// virtual sizes
	card_size   = file.fileSize();
	sector_size = 512;

	Serial.printf("Virtual disk size: %zuMB\r\n", size_t(card_size / 1024 / 1024));

	Serial.println(F("Init LEDs"));

	return true;
}

backend_sdcard::~backend_sdcard()
{
	file.close();
	sd.end();
}

bool backend_sdcard::sync()
{
#ifdef LED_RED
	digitalWrite(LED_RED, HIGH);
#endif
	n_syncs++;

	if (file.sync() == false)
		Serial.println(F("SD card backend: sync failed"));

#ifdef LED_RED
	digitalWrite(LED_RED, LOW);
#endif

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
#ifdef LED_RED
	digitalWrite(LED_RED, HIGH);
#endif

	uint64_t iscsi_block_size = get_block_size();
	uint64_t byte_address     = block_nr * iscsi_block_size;  // iSCSI to bytes

	if (file.seekSet(byte_address) == false) {
		Serial.println(F("Cannot seek to position"));
#ifdef LED_RED
		digitalWrite(LED_RED, LOW);
#endif
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
		Serial.printf("Cannot write (%d)\r\n", file.getError());
#ifdef LED_RED
		digitalWrite(LED_RED, LOW);
#endif
	return rc;
}

bool backend_sdcard::read(const uint64_t block_nr, const uint32_t n_blocks, uint8_t *const data)
{
#ifdef LED_GREEN
	digitalWrite(LED_GREEN, HIGH);
#endif
	// Serial.printf("Read from block %zu, %u blocks\r\n", size_t(block_nr), n_blocks);

	uint64_t iscsi_block_size = get_block_size();
	uint64_t byte_address     = block_nr * iscsi_block_size;  // iSCSI to bytes

	if (file.seekSet(byte_address) == false) {
		Serial.println(F("Cannot seek to position"));
#ifdef LED_GREEN
		digitalWrite(LED_GREEN, LOW);
#endif
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
		Serial.printf("Cannot read (%d)\r\n", file.getError());
#ifdef LED_GREEN
	digitalWrite(LED_GREEN, LOW);
#endif
	return rc;
}
