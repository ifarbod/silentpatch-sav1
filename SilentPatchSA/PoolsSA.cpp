#include "StdAfxSA.h"
#include "PoolsSA.h"

ExternalRef<CObjectPool*> CPools::ms_pObjectPool(AddressByVersion<CObjectPool***>(0x5A18B2 + 2));