#include "backend.h"

backend::backend()
{
}

backend::~backend()
{
}

void backend::get_and_reset_stats(uint64_t *const bytes_read, uint64_t *const bytes_written, uint64_t *const n_syncs)
{
	*bytes_read    = this->bytes_read;
	*bytes_written = this->bytes_written;
	*n_syncs       = this->n_syncs;

	this->bytes_read    = 0;
	this->bytes_written = 0;
	this->n_syncs       = 0;
}
