#include <string>

#include "backend.h"


class backend_nbd : public backend
{
private:
	const std::string host;
	const int         port     { 0  };
	int               fd       { -1 };
	uint64_t          dev_size { 0  };

	bool connect   (const bool retry);
	bool invoke_nbd(const uint32_t command, const uint64_t offset, const uint32_t n_bytes, uint8_t *const data);

public:
	backend_nbd(const std::string & host, const int port);
	virtual ~backend_nbd();

	bool begin() override;

	std::string get_serial()         const override;
	uint64_t    get_size_in_blocks() const override;
	uint64_t    get_block_size()     const override;

	bool sync() override;

	bool write   (const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data) override;
	bool trim    (const uint64_t block_nr, const uint32_t n_blocks                           ) override;
	bool read    (const uint64_t block_nr, const uint32_t n_blocks,       uint8_t *const data) override;
	backend::cmpwrite_result_t cmpwrite(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data_write, const uint8_t *const data_compare) override;
};
