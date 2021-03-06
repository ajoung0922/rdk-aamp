#include "opencdmsessionadapter.h"
#include "AampDrmHelper.h"

class AAMPOCDMBasicSessionAdapter : public AAMPOCDMSessionAdapter
{
public:
	AAMPOCDMBasicSessionAdapter(std::shared_ptr<AampDrmHelper> drmHelper, AampDrmCallbacks *drmCallbacks)
	: AAMPOCDMSessionAdapter(drmHelper, drmCallbacks)
	{};
	~AAMPOCDMBasicSessionAdapter() {};

	int decrypt(const uint8_t *f_pbIV, uint32_t f_cbIV, const uint8_t *payloadData, uint32_t payloadDataSize, uint8_t **ppOpaqueData);
};
