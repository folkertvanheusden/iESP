#include <string>

#include "backend.h"


class backend_file : public backend
{
private:
	int fd { -1 };
public:
	backend_file(const std::string & filename);
	virtual ~backend_file();

	uint64_t get_size_in_blocks() const override;
	uint64_t get_block_size()     const override;

	bool sync() override;

	bool write(const uint64_t block_nr, const uint32_t n_blocks, const uint8_t *const data) override;
	bool trim (const uint64_t block_nr, const uint32_t n_blocks                           ) override;
	bool read (const uint64_t block_nr, const uint32_t n_blocks,       uint8_t *const data) override;
};
