#include <cstdint>


class session
{
private:
	uint8_t ISID[6];

public:
	session(const uint8_t ISID[6]);
	virtual ~session();
};
