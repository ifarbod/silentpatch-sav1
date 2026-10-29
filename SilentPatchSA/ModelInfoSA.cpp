#include "StdAfxSA.h"
#include "ModelInfoSA.h"

#include <algorithm>

ExternalFunc<RwTexture* (const char* pText, signed char nDesign)> CCustomCarPlateMgr::CreatePlateTexture(AddressByVersion<RwTexture*(*)(const char*,signed char)>(0x6FDEA0));
ExternalFunc<signed char ()> CCustomCarPlateMgr::GetMapRegionPlateDesign(AddressByVersion<signed char(*)()>(0x6FD7A0));
ExternalFunc<void (RpMaterial* pMaterial, signed char nDesign)> CCustomCarPlateMgr::SetupMaterialPlatebackTexture(AddressByVersion<void(*)(RpMaterial*,signed char)>(0x6FDE50));

ExternalFunc<bool (char* pBuf, int nLen)> CCustomCarPlateMgr::GeneratePlateText; // Read from InjectDelayedPatches

ExternalRef<CBaseModelInfo*[]> ms_modelInfoPtrs(AddressByVersion<CBaseModelInfo*(**)[]>(0x509CB1));

ExternalRef<int8_t[2]> CVehicleModelInfo::ms_compsUsed(AddressByVersion<int8_t(**)[2]>(0x4C973B + 2));
ExternalRef<int8_t[2]> CVehicleModelInfo::ms_compsToUse(AddressByVersion<int8_t(**)[2]>(0x4C8057 + 2));

static ExternalRef ms_aDirtTextures(AddressByVersion<RwTexture*(**)[16]>(0x5D5DCC + 3));

bool HasGameBindings_DirtRemapFix()
{
	return rwengine != nullptr && RWBindings::RpMaterialSetTexture() && EnsureBindings(ms_aDirtTextures);
}

bool HasGameBindings_ResetCompsForNoExtras()
{
	return EnsureBindings(CVehicleModelInfo::ms_compsUsed, CVehicleModelInfo::ms_compsToUse);
}

void RemapDirt( CVehicleModelInfo* modelInfo, uint32_t dirtID )
{
	auto& aDirtTextures = ms_aDirtTextures.Get();

	std::size_t i = 0;
	for (std::size_t j = std::min(modelInfo->m_numDirtMaterials, std::size(modelInfo->m_staticDirtMaterials)); i < j; ++i)
	{
		RpMaterialSetTexture(modelInfo->m_staticDirtMaterials[i], aDirtTextures[dirtID]);
	}

	// Now apply the remaining textures, if any
	if (modelInfo->m_dirtMaterials != nullptr)
	{
		const size_t offset = std::size(modelInfo->m_staticDirtMaterials);
		for (; i < modelInfo->m_numDirtMaterials; ++i)
		{
			RpMaterialSetTexture(modelInfo->m_dirtMaterials[i - offset], aDirtTextures[dirtID]);
		}
	}
}

uint32_t CVehicleModelInfo::GetNumRemaps() const
{
	uint32_t count = 0;
	while ( m_awRemapTxds[count].Get() != -1 && count < _countof(m_awRemapTxds) )
	{
		count++;
	}
	return count;
}

void (CClumpModelInfo::*CVehicleModelInfo::orgShutdown_CarDirtFix)();
void CVehicleModelInfo::Shutdown_CarDirtFix()
{
	delete[] m_dirtMaterials;
	m_dirtMaterials = nullptr;

	std::invoke(orgShutdown_CarDirtFix, this);
}

void CVehicleModelInfo::FindEditableMaterialList()
{
	std::vector<RpMaterial*> editableMaterials;

	auto GetEditableMaterialListCB = [&editableMaterials]( RpAtomic* atomic ) -> RpAtomic* {
		RpGeometryForAllMaterials( RpAtomicGetGeometry(atomic), [&]( RpMaterial* material ) -> RpMaterial* {
			if ( RwTexture* texture = RpMaterialGetTexture(material) )
			{
				if ( const char* texName = RwTextureGetName(texture) )
				{
					if ( rwstrcmp(texName, "vehiclegrunge256") == 0 )
					{
						editableMaterials.push_back( material );
					}
				}
			}
			return material;
		} );
		return atomic;
	};

	RpClumpForAllAtomics(reinterpret_cast<RpClump*>(pRwObject), GetEditableMaterialListCB);

	for ( uint32_t i = 0; i < m_pVehicleStruct->m_nNumExtras; i++ )
		GetEditableMaterialListCB(m_pVehicleStruct->m_apExtras[i]);

	m_numDirtMaterials = editableMaterials.size();

	// We always use the stock array first, then allocate if we need more space
	const std::size_t inPlaceBufferCopySize = std::min(m_numDirtMaterials, std::size(m_staticDirtMaterials));
	std::copy_n(editableMaterials.begin(), inPlaceBufferCopySize, m_staticDirtMaterials);

	if (m_numDirtMaterials > inPlaceBufferCopySize)
	{
		const std::size_t remainder = m_numDirtMaterials - inPlaceBufferCopySize;
		m_dirtMaterials = new RpMaterial* [remainder];
		std::copy_n(editableMaterials.begin() + inPlaceBufferCopySize, remainder, m_dirtMaterials);
	}
	else
	{
		m_dirtMaterials = nullptr;
	}

	m_nPrimaryColor = -1;
	m_nSecondaryColor = -1;
	m_nTertiaryColor = -1;
	m_nQuaternaryColor = -1;
}

void CVehicleModelInfo::SetCarCustomPlate()
{
	m_plateText[0] = '\0';
	m_nPlateType = -1;
}

void CVehicleModelInfo::ResetCompsForNoExtras()
{
	auto& compsUsed = ms_compsUsed.Get();
	auto& compsToUse = ms_compsToUse.Get();

	compsUsed[0] = compsUsed[1] = -1;
	compsToUse[0] = compsToUse[1] = -2;
}

bool CCustomCarPlateMgr::HasGameBindings()
{
	// Deliberately not checking CCustomCarPlateMgr::GeneratePlateText, as it's bound later
	return EnsureBindings(CreatePlateTexture, GetMapRegionPlateDesign, SetupMaterialPlatebackTexture);
}

void CCustomCarPlateMgr::SetupClumpAfterVehicleUpgrade(RpClump* pClump, void* /*unused*/, signed char nDesign)
{
	UNREFERENCED_PARAMETER(nDesign);
}