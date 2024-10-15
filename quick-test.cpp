#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <iscsi/iscsi.h>
#include <iscsi/scsi-lowlevel.h>


constexpr int      bs  = 4096;
constexpr long int lba = 0;
constexpr int      lun = 1;
constexpr int      bc  = 3;  // block-count
bool               ok  = true;

void test_read_write(iscsi_context *const iscsi, const uint8_t fill)
{
	printf("Read/write test for filler %02x\n", fill);

	uint8_t *buffer = new uint8_t[bs * bc];

	// just to see if they can be invoked
	std::vector<std::tuple<std::string,
		std::function<struct scsi_task *(struct iscsi_context *iscsi, int lun, uint32_t lba, unsigned char *data, uint32_t datalen, int blocksize, int wrprotect, int dpo, int fua, int fua_nv, int group_number)>,  // write
		std::function<struct scsi_task *(struct iscsi_context *iscsi, int lun, uint32_t lba, uint32_t datalen, int blocksize, int rdprotect, int dpo, int fua, int fua_nv, int group_number)>  // read
			> > rw_functions = {
				{ "R/W-10", iscsi_write10_sync, iscsi_read10_sync },
				{ "R/W-12", iscsi_write12_sync, iscsi_read12_sync },
				{ "R/W-16", iscsi_write16_sync, iscsi_read16_sync }
			};

	memset(buffer, fill, bs);
	for(auto & e: rw_functions) {
		printf(" Testing: %s\n", std::get<0>(e).c_str());

		scsi_task *task_w = std::get<1>(e)(iscsi, lun, lba, buffer, bs * bc, bs, 0, 0, 0, 0, 0);
		if (task_w == nullptr || task_w->status != SCSI_STATUS_GOOD) {
			printf("  write failed: %s\n", iscsi_get_error(iscsi));
			scsi_free_scsi_task(task_w);
			ok = false;
			continue;
		}
		scsi_free_scsi_task(task_w);

		scsi_task *task_r = std::get<2>(e)(iscsi, lun, lba, bs * bc, bs, 0, 0, 0, 0, 0);
		if (task_r == nullptr || task_r->status != SCSI_STATUS_GOOD) {
			printf("  read failed: %s\n", iscsi_get_error(iscsi));
			ok = false;
		}
		for(int i=0; i<bs; i++) {
			if (task_r->datain.data[i] != fill) {
				printf("   Data mismatch at offset %i: %02x where should've been %02x\n", i, task_r->datain.data[i], fill);
				ok = false;
			}
		}
		scsi_free_scsi_task(task_r);
	}

	delete [] buffer;

	printf("\n");
}

int main(int argc, char *argv[])
{
	iscsi_context *iscsi = iscsi_create_context("iqn.2024-2.com.vanheusden:client");
	if (iscsi == NULL) {
		printf("Failed to create context\n");
		exit(10);
	}

	if (iscsi_connect_sync(iscsi, "localhost") != 0) {
		printf("iscsi_connect failed: %s\n", iscsi_get_error(iscsi));
		exit(10);
	}

	iscsi_set_session_type (iscsi, ISCSI_SESSION_NORMAL);
        iscsi_set_header_digest(iscsi, ISCSI_HEADER_DIGEST_CRC32C_NONE);

        if (iscsi_set_targetname(iscsi, "test")) {
                printf("Failed to set target name\n");
                exit(10);
        }

	if (iscsi_set_initial_r2t(iscsi, ISCSI_INITIAL_R2T_YES)) {
		printf("ISCSI_INITIAL_R2T_YES failed: %s\n", iscsi_get_error(iscsi));
		exit(10);
	}

	if (iscsi_set_header_digest(iscsi, ISCSI_HEADER_DIGEST_CRC32C_NONE)) {
		printf("ISCSI_HEADER_DIGEST_CRC32C_NONE failed: %s\n", iscsi_get_error(iscsi));
                exit(10);
        }

#if 0  // Not available in Ubuntu 24.04
	if (iscsi_set_data_digest(iscsi, ISCSI_DATA_DIGEST_CRC32C_NONE)) {
		printf("ISCSI_DATA_DIGEST_CRC32C_NONE failed: %s\n", iscsi_get_error(iscsi));
                exit(10);
        }
#endif

	if (iscsi_login_sync(iscsi)) {
		printf("iscsi_login failed: %s\n", iscsi_get_error(iscsi));
		exit(10);
	}

	if (iscsi_testunitready_sync(iscsi, 0) == NULL) {
		printf("failed to send testunitready command: %s\n", iscsi_get_error(iscsi));
		exit(10);
	}

        scsi_task *task = NULL;
        task = iscsi_inquiry_sync(iscsi, 0, 0, 0, 64);
        if (task == NULL || task->status != SCSI_STATUS_GOOD) {
                fprintf(stderr, "failed to send inquiry command: %s\n", iscsi_get_error(iscsi));
                exit(10);
        }

	test_read_write(iscsi, 0x00);
	test_read_write(iscsi, 0xff);
	test_read_write(iscsi, 0x99);

	scsi_task *task_synchronizecache10 = iscsi_synchronizecache10_sync(iscsi, lun, lba, 1, 1, 1);
	if (task_synchronizecache10 == NULL || task_synchronizecache10->status != SCSI_STATUS_GOOD) {
		printf(" SYNC10 failed: %s\n", iscsi_get_error(iscsi));
		return 1;
	}
	scsi_free_scsi_task(task_synchronizecache10);

	scsi_task *task_synchronizecache16 = iscsi_synchronizecache16_sync(iscsi, lun, lba, 1, 1, 1);
	if (task_synchronizecache16 == NULL || task_synchronizecache16->status != SCSI_STATUS_GOOD) {
		printf(" SYNC16 failed: %s\n", iscsi_get_error(iscsi));
		return 1;
	}
	scsi_free_scsi_task(task_synchronizecache16);

	iscsi_destroy_context(iscsi);

	assert(ok);

	return !ok;
}
