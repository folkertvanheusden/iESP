#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <SD.h>

#include "backend-sdcard.h"
#include "log.h"


backend_sdcard::backend_sdcard()
{
	if (!SD.begin(5)) {
		Serial.println("SD-card mount failed");
		return;
	}

	auto card_type = SD.cardType();

	Serial.print("SD-card type: ");

	if (card_type == CARD_MMC)
		Serial.println("MMC");
	else if (card_type == CARD_SD)
		Serial.println("SDSC");
	else if (card_type == CARD_SDHC)
		Serial.println("SDHC");
	else
		Serial.println("UNKNOWN");

	card_size = SD.cardSize();
	Serial.printf("SD-card size: %zuMB\r\n", size_t(card_size / 1024 / 1024));
	sector_size = SD.sectorSize();
	Serial.printf("Sector size: %zu\r\n", sector_size);
}

backend_sdcard::~backend_sdcard()
{
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
	uint64_t iscsi_block_size = get_block_size();

	for(uint32_t i=0; i<n_blocks; i++) {
		const uint8_t *iscsi_block_offset = &data[i * iscsi_block_size];

		uint64_t byte_address  = (block_nr + i) * iscsi_block_size;  // iSCSI to bytes
		uint32_t sd_block_nr   = byte_address / sector_size;   // bytes to SD

		for(int s=0; s<iscsi_block_size / sector_size; s++) {  // an iSCSI block is usually 4kB (in this implementation) and an SD-card has 0.5kB sectors
			const uint8_t *sd_block_offset = iscsi_block_offset + s * sector_size;

			if (SD.writeRAW(const_cast<uint8_t *>(sd_block_offset), sd_block_nr + s) == false)
				return false;
		}
	}

	return true;
}

bool backend_sdcard::read(const uint64_t block_nr, const uint32_t n_blocks, uint8_t *const data)
{
	uint64_t iscsi_block_size = get_block_size();

	for(uint32_t i=0; i<n_blocks; i++) {
		uint8_t *iscsi_block_offset = &data[i * iscsi_block_size];

		uint64_t byte_address  = (block_nr + i) * iscsi_block_size;  // iSCSI to bytes
		uint32_t sd_block_nr   = byte_address / sector_size;   // bytes to SD

		for(int s=0; s<iscsi_block_size / sector_size; s++) {  // an iSCSI block is usually 4kB (in this implementation) and an SD-card has 0.5kB sectors
			uint8_t *sd_block_offset = iscsi_block_offset + s * sector_size;

			if (SD.readRAW(sd_block_offset, sd_block_nr + s) == false)
				return false;
		}
	}

	return true;
}
