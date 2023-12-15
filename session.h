#include <cstdint>


class session
{
private:
	uint8_t ISID[6] {   };

	uint16_t CID    { 0 };

public:
	session(const uint8_t ISID[6], const uint16_t CID);
	virtual ~session();
};
