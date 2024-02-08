#include "com.h"

com_client::com_client(std::atomic_bool *const stop): stop(stop)
{
}

com_client::~com_client()
{
}

com::com(std::atomic_bool *const stop): stop(stop)
{
}

com::~com()
{
}
