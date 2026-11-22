// Read-only LUN whose contents are compiled into the firmware (no SD card needed).
// Useful for a tiny network key device, e.g. a LUKS key file or ZFS key source.
// Enable with -DIESP_BACKEND_VIRTUAL_FLASH (see platformio.ini, env BUILD_FOR_RP2040W_VFLASH).
//
// The data comes from key-data.h (git-ignored), which must define
//   unsigned char disk_img[];  unsigned int disk_img_len;   (length a multiple of 512)
// e.g.:  xxd -i -n disk_img key.bin > key-data.h      (see key-data.h.example)
#pragma once

#include <Arduino.h>
#include <cstring>

#include "backend.h"
#include "gen.h"
#include "log.h"
#include "utils.h"

#include "key-data.h"

extern void write_led(const int gpio, const int state);

class backend_virtual_flash : public backend
{
private:
	const int led_read { -1 };

public:
	backend_virtual_flash(const int led_read): backend("virtual flash"), led_read(led_read) {
	}

	virtual ~backend_virtual_flash() {
	}

	bool begin() override {
		write_led(led_read, LOW);

		if (disk_img_len == 0 || disk_img_len % 512) {
			Serial.println(F("virtual flash: image length must be a non-zero multiple of 512 bytes"));
			return false;
		}

		return true;
	}

	std::string get_serial()         const override { return DEFAULT_SERIAL;     }
	uint64_t    get_size_in_blocks() const override { return disk_img_len / 512; }
	uint64_t    get_block_size()     const override { return 512;                }

	bool sync() override {
		return true;
	}

	bool read(const uint64_t block_nr, const uint32_t n_blocks, uint8_t *const data) override {
		const uint64_t offset = block_nr * 512;
		const uint64_t n      = uint64_t(n_blocks) * 512;

		if (offset + n > disk_img_len) {
			memset(data, 0x00, n);
			return false;
		}

		write_led(led_read, HIGH);
		memcpy(data, &disk_img[offset], n);
		write_led(led_read, LOW);

		return true;
	}

	// read-only device
	bool write(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data) override {
		return false;
	}

	bool trim(const uint64_t block_nr, const uint32_t n_blocks) override {
		return false;
	}

	backend::cmpwrite_result_t cmpwrite(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data_write, const uint8_t *const data_compare) override {
		return CWR_WRITE_ERROR;
	}
};
