#include "StdAfxSA.h"
#include <algorithm>
#include <array>
#include <d3d9.h>
#include <Shlwapi.h>
#include <ShlObj.h>
#include <ShellAPI.h>
#include <cinttypes>

#include "AnimationSA.h"
#include "ScriptSA.h"
#include "GeneralSA.h"
#include "ModelInfoSA.h"
#include "VehicleSA.h"
#include "PedSA.h"
#include "AudioHardwareSA.h"
#include "LinkListSA.h"
#include "PNGFile.h"
#include "PlayerInfoSA.h"
#include "FireManagerSA.h"
#include "SpeechContextsSA.h"
#include "Random.h"

#include "WaveDecoderSA.h"
#include "MiniaudioDecoderSA.h"

#include "Utils/Patterns.h"
#include "Utils/ModuleList.hpp"
#include "Utils/ScopedUnprotect.hpp"
#include "Utils/HookEach.hpp"

#include "Desktop.h"
#include "DelimStringReader.hpp"
#include "FriendlyMonitorNames.h"
#include "SVF.h"

#include "debugmenu_public.h"
#include "resource.h"

#pragma comment(linker,"\"/manifestdependency:type='win32' \
		name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
		processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

// ============= Mod compatibility stuff =============

namespace ModCompat
{
	namespace SkyGfx
	{
		struct Config
		{
			uint32_t version;
			// The rest isn't relevant at the moment
		};

		const Config* GetConfig(HMODULE module)
		{
			auto func = (Config*(*)())GetProcAddress(module, "GetConfig");
			if (func == nullptr) return nullptr; // Old version?
			return func();
		}

		bool PatchesMoonphases(HMODULE module)
		{
			if (module == nullptr) return false; // SkyGfx not installed

			const Config* config = GetConfig(module);
			if (config == nullptr) return false; // Old version/error?

			constexpr uint32_t SKYGFX_VERSION_WITH_MOONPHASES = 0x360;
			return config->version >= SKYGFX_VERSION_WITH_MOONPHASES;
		}
	}

	bool bCdStreamFallBackForOldML = false;
	bool ModloaderCdStreamRaceConditionAware( HMODULE module )
	{
		if ( module == nullptr ) return false; // modloader not installed

		HMODULE stdStreamModule;
		if ( GetModuleHandleEx( 0, TEXT("std.stream.dll"), &stdStreamModule ) == 0 ) return false; // std.data not loaded

		// ML is installed, so if it's an old version we need to fall back to a less safe implementation (no condition variables)
		bCdStreamFallBackForOldML = true;

		bool aware = false;
		const auto func = (uint32_t(*)())GetProcAddress( stdStreamModule, "CdStreamRaceConditionAware" );
		if ( func != nullptr )
		{
			aware = func() >= 1;
		}
		FreeLibrary( stdStreamModule );
		return aware;
	}

	namespace Utils
	{
		template<typename AT>
		HMODULE GetModuleHandleFromAddress( AT address )
		{
			HMODULE result = nullptr;
			GetModuleHandleEx( GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT|GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, LPCTSTR(address), &result );
			return result;
		}

		// Resolves a re-route if it comes from a no-CD executable
		uintptr_t GetFunctionAddrIfRerouted(uintptr_t address)
		{
			uint8_t jmp;
			Memory::Read(address, jmp);
			if (jmp == 0xE9u)
			{
				uintptr_t jumpDestination;
				Memory::ReadCall(address, jumpDestination);
				if (GetModuleHandleFromAddress(address) == GetModuleHandleFromAddress(jumpDestination))
				{
					return jumpDestination;
				}
			}
			return address;
		}
	}
}

#pragma warning(disable:4733)

// RW wrappers
namespace RWBindings
{
#define ENSURE_FUNC(func) bool func() { return fn##func.Ensure(); }

	static ExternalFunc fnAtomicDefaultRenderCallBack(AddressByVersion<RpAtomic* (*)(RpAtomic* atomic)>(0x7491C0));
	ENSURE_FUNC(AtomicDefaultRenderCallBack);

	static ExternalFunc fnRpMaterialSetTexture(AddressByVersion<RpMaterial* (*)(RpMaterial* material, RwTexture* texture)>(0x74DBC0));
	ENSURE_FUNC(RpMaterialSetTexture);

	static ExternalFunc fnRwFrameGetLTM(AddressByVersion<RwMatrix* (*)(RwFrame* frame)>(0x7F0990));
	ENSURE_FUNC(RwFrameGetLTM);

	static ExternalFunc fnRwEngineSetSubSystem(AddressByVersion<RwBool (*)(RwInt32)>(0x7F2C90));
	ENSURE_FUNC(RwEngineSetSubSystem);

	static ExternalFunc fnRwD3D9SetRenderState(AddressByVersion<void (*)(RwUInt32 state, RwUInt32 value)>(0x7FC2D0));
	ENSURE_FUNC(RwD3D9SetRenderState);

	static ExternalFunc fnRtPNGImageRead(AddressByVersion<RwImage* (*)(const RwChar* imageName) >(0x7CF9B0));
	ENSURE_FUNC(RtPNGImageRead);

	static ExternalFunc fnRwTextureCreate(AddressByVersion<RwTexture* (*)(RwRaster* raster)>(0x7F37C0));
	ENSURE_FUNC(RwTextureCreate);

	static ExternalFunc fnRwRasterCreate(AddressByVersion<RwRaster* (*)(RwInt32 width, RwInt32 height, RwInt32 depth, RwInt32 flags)>(0x7FB230));
	ENSURE_FUNC(RwRasterCreate);

	static ExternalFunc fnRwImageDestroy(AddressByVersion<RwBool (*)(RwImage* image)>(0x802740));
	ENSURE_FUNC(RwImageDestroy);

	static ExternalFunc fnRwMatrixRotate(AddressByVersion<RwMatrix* (*)(RwMatrix* matrix, const RwV3d* axis, RwReal angle, RwOpCombineType combineOp)>(0x7F1FD0));
	ENSURE_FUNC(RwMatrixRotate);

#undef ENSURE_FUNC
}

RpAtomic* AtomicDefaultRenderCallBack(RpAtomic* atomic) { return RWBindings::fnAtomicDefaultRenderCallBack.Call(atomic); }
RwImage* RtPNGImageRead(const RwChar* imageName) { return RWBindings::fnRtPNGImageRead.Call(imageName); }
RwTexture* RwTextureCreate(RwRaster* raster) { return RWBindings::fnRwTextureCreate.Call(raster); }
RwRaster* RwRasterCreate(RwInt32 width, RwInt32 height, RwInt32 depth, RwInt32 flags) { return RWBindings::fnRwRasterCreate.Call(width, height, depth, flags); }
RwBool RwImageDestroy(RwImage* image) { return RWBindings::fnRwImageDestroy.Call(image); }
RwMatrix* RwMatrixRotate(RwMatrix* matrix, const RwV3d* axis, RwReal angle, RwOpCombineType combineOp) { return RWBindings::fnRwMatrixRotate.Call(matrix, axis, angle, combineOp); }

RpMaterial* RpMaterialSetTexture(RpMaterial* material, RwTexture* texture) { return RWBindings::fnRpMaterialSetTexture.Call(material, texture); }
RwMatrix* RwFrameGetLTM(RwFrame* frame) { return RWBindings::fnRwFrameGetLTM.Call(frame); }
void RwD3D9SetRenderState(RwUInt32 state, RwUInt32 value) { RWBindings::fnRwD3D9SetRenderState.Call(state, value); }
RwBool RwEngineSetSubSystem(RwInt32 subSystemIndex) { return RWBindings::fnRwEngineSetSubSystem.Call(subSystemIndex); }

RwCamera* RwCameraBeginUpdate(RwCamera* camera)
{
	return camera->beginUpdate(camera);
}

RwCamera* RwCameraEndUpdate(RwCamera* camera)
{
	return camera->endUpdate(camera);
}

RwCamera* RwCameraClear(RwCamera* camera, RwRGBA* colour, RwInt32 clearMode)
{
	return RWSRCGLOBAL(stdFunc[rwSTANDARDCAMERACLEAR])(camera, colour, clearMode) != FALSE ? camera : NULL;
}

RwMatrix* RwMatrixTranslate(RwMatrix* matrix, const RwV3d* translation, RwOpCombineType combineOp)
{
	if ( combineOp == rwCOMBINEREPLACE )
	{
		RwMatrixSetIdentity(matrix);
		matrix->pos = *translation;
	}
	else if ( combineOp == rwCOMBINEPRECONCAT )
	{
		matrix->pos.x += matrix->at.x * translation->z + matrix->up.x * translation->y + matrix->right.x * translation->x;
		matrix->pos.y += matrix->at.y * translation->z + matrix->up.y * translation->y + matrix->right.y * translation->x;
		matrix->pos.z += matrix->at.z * translation->z + matrix->up.z * translation->y + matrix->right.z * translation->x;
	}
	else if ( combineOp == rwCOMBINEPOSTCONCAT )
	{
		matrix->pos.x += translation->x;
		matrix->pos.y += translation->y;
		matrix->pos.z += translation->z;
	}
	rwMatrixSetFlags(matrix, rwMatrixGetFlags(matrix) & ~(rwMATRIXINTERNALIDENTITY));
	return matrix;
}

RwFrame* RwFrameForAllChildren(RwFrame* frame, RwFrameCallBack callBack, void* data)
{
	for ( RwFrame* curFrame = frame->child; curFrame != nullptr; curFrame = curFrame->next )
	{
		if ( callBack(curFrame, data) == NULL )
			break;
	}
	return frame;
}

RwFrame* RwFrameForAllObjects(RwFrame* frame, RwObjectCallBack callBack, void* data)
{
	for ( RwLLLink* link = rwLinkListGetFirstLLLink(&frame->objectList); link != rwLinkListGetTerminator(&frame->objectList); link = rwLLLinkGetNext(link) )
	{
		if ( callBack(&rwLLLinkGetData(link, RwObjectHasFrame, lFrame)->object, data) == NULL )
			break;
	}

	return frame;
}

RwFrame* RwFrameUpdateObjects(RwFrame* frame)
{
	if ( !rwObjectTestPrivateFlags(&frame->root->object, rwFRAMEPRIVATEHIERARCHYSYNCLTM|rwFRAMEPRIVATEHIERARCHYSYNCOBJ) )
		rwLinkListAddLLLink(&RWSRCGLOBAL(dirtyFrameList), &frame->root->inDirtyListLink);

	rwObjectSetPrivateFlags(&frame->root->object, rwObjectGetPrivateFlags(&frame->root->object) | (rwFRAMEPRIVATEHIERARCHYSYNCLTM|rwFRAMEPRIVATEHIERARCHYSYNCOBJ));
	rwObjectSetPrivateFlags(&frame->object, rwObjectGetPrivateFlags(&frame->object) | (rwFRAMEPRIVATESUBTREESYNCLTM|rwFRAMEPRIVATESUBTREESYNCOBJ));
	return frame;
}

RwMatrix* RwMatrixUpdate(RwMatrix* matrix)
{
	matrix->flags &= ~(rwMATRIXTYPEMASK|rwMATRIXINTERNALIDENTITY);
	return matrix;
}

RwRaster* RwRasterSetFromImage(RwRaster* raster, RwImage* image)
{
	if ( RWSRCGLOBAL(stdFunc[rwSTANDARDRASTERSETIMAGE])(raster, image, 0) != FALSE )
	{
		if ( image->flags & rwIMAGEGAMMACORRECTED )
			raster->privateFlags |= rwRASTERGAMMACORRECTED;
		return raster;
	}
	return NULL;
}

RwImage* RwImageFindRasterFormat(RwImage* ipImage, RwInt32 nRasterType, RwInt32* npWidth, RwInt32* npHeight, RwInt32* npDepth, RwInt32* npFormat)
{
	RwRaster	outRaster;
	if ( RWSRCGLOBAL(stdFunc[rwSTANDARDIMAGEFINDRASTERFORMAT])(&outRaster, ipImage, nRasterType) != FALSE )
	{
		*npFormat = RwRasterGetFormat(&outRaster) | outRaster.cType;
		*npWidth = RwRasterGetWidth(&outRaster);
		*npHeight = RwRasterGetHeight(&outRaster);
		*npDepth = RwRasterGetDepth(&outRaster);
		return ipImage;
	}
	return NULL;
}

RpClump* RpClumpForAllAtomics(RpClump* clump, RpAtomicCallBack callback, void* pData)
{
	for ( RwLLLink* link = rwLinkListGetFirstLLLink(&clump->atomicList); link != rwLinkListGetTerminator(&clump->atomicList); link = rwLLLinkGetNext(link) )
	{
		if ( callback(rwLLLinkGetData(link, RpAtomic, inClumpLink), pData) == NULL )
			break;
	}
	return clump;
}

RpClump* RpClumpRender(RpClump* clump)
{
	RpClump*	retClump = clump;

	for ( RwLLLink* link = rwLinkListGetFirstLLLink(&clump->atomicList); link != rwLinkListGetTerminator(&clump->atomicList); link = rwLLLinkGetNext(link) )
	{
		RpAtomic* curAtomic = rwLLLinkGetData(link, RpAtomic, inClumpLink);
		if ( RpAtomicGetFlags(curAtomic) & rpATOMICRENDER )
		{
			// Not sure why they need this
			RwFrameGetLTM(RpAtomicGetFrame(curAtomic));
			if ( RpAtomicRender(curAtomic) == NULL )
				retClump = NULL;
		}
	}
	return retClump;
}

RpGeometry* RpGeometryForAllMaterials(RpGeometry* geometry, RpMaterialCallBack fpCallBack, void* pData)
{
	for ( RwInt32 i = 0, j = geometry->matList.numMaterials; i < j; i++ )
	{
		if ( fpCallBack(geometry->matList.materials[i], pData) == NULL )
			break;
	}
	return geometry;
}

RwInt32 RpHAnimIDGetIndex(RpHAnimHierarchy* hierarchy, RwInt32 ID)
{
	for ( RwInt32 i = 0, j = hierarchy->numNodes; i < j; i++ )
	{
		if ( ID == hierarchy->pNodeInfo[i].nodeID )
			return i;
	}
	return -1;
}

RwMatrix* RpHAnimHierarchyGetMatrixArray(RpHAnimHierarchy* hierarchy)
{
	return hierarchy->pMatrixArray;
}

void RwD3D9DeleteVertexShader(void* shader)
{
	static_cast<IUnknown*>(shader)->Release();
}

RwBool _rpD3D9VertexDeclarationInstColor(RwUInt8* mem, const RwRGBA* color, RwInt32 numVerts, RwUInt32 stride)
{
	RwUInt8 alpha = 255;
	for ( RwInt32 i = 0; i < numVerts; i++ )
	{
		*reinterpret_cast<RwUInt32*>(mem) = (color->alpha << 24) | (color->red << 16) | (color->green << 8) | color->blue;
		alpha &= color->alpha;
		color++;
		mem += stride;
	}
	return alpha != 255;
}

// Unreachable stub
RwBool RwMatrixDestroy(RwMatrix* mpMat) { assert(!"Unreachable!"); return TRUE; }

struct AlphaObjectInfo
{
	RpAtomic*	pAtomic;
	RpAtomic*	(*callback)(RpAtomic*, float);
	float		fCompareValue;

	friend bool operator < (const AlphaObjectInfo &a, const AlphaObjectInfo &b)
	{ return a.fCompareValue < b.fCompareValue; }
};

struct PsGlobalType
{
	HWND	window;
	DWORD	instance;
	DWORD	fullscreen;
	DWORD	lastMousePos_X;
	DWORD	lastMousePos_Y;
	DWORD	unk;
	DWORD	diInterface;
	DWORD	diMouse;
	void*	diDevice1;
	void*	diDevice2;
};

struct RsGlobalType
{
	const char*		AppName;
	signed int		MaximumWidth;
	signed int		MaximumHeight;
	unsigned int	frameLimit;
	BOOL			quit;
	PsGlobalType*	ps;
	void*			keyboard;
	void*			mouse;
	void*			pad;
};

// Other wrappers
ExternalFunc<void (void*)> GTAdelete(AddressByVersion<void(*)(void*)>(0x82413F));

ExternalFunc			GetFrameNodeName(AddressByVersion<const char*(*)(RwFrame*)>(0x72FB30));
ExternalFunc			GetAnimHierarchyFromSkinClump(AddressByVersion<RpHAnimHierarchy*(*)(RpClump*)>(0x734A40));
ExternalFunc			RpAnimBlendClumpGetAssociation(AddressByVersion<CAnimBlendAssociation*(*)(RpClump*, uint32_t)>(0x4D68B0));
ExternalFunc			GetAnimationBlockIndex(AddressByVersion<int32_t(*)(const char* animBlock)>(0x4D3990));
ExternalFunc			RequestModel(AddressByVersion<void(*)(int modelID, int priority)>(0x4087E0));
ExternalFunc			LoadAllRequestedModels(AddressByVersion<void(*)(bool bBlock)>(0x40EA10));
ExternalFunc			ClearAtomicFlag(AddressByVersion<void(*)(RpAtomic*, int)>(0x732310));

ExternalFunc			IsPlayerOnAMission(AddressByVersion<bool(*)()>(0x464D50));

ExternalFunc			WorldRemove(AddressByVersion<void(*)(CEntity*)>(0x563280));
ExternalFunc			IsInPlayersGroup(AddressByVersion<bool(*)(const CPed* pPed)>(0x5F7F10));


ExternalFunc<bool(int modelID)> IsTrainModel(AddressByVersion<bool (*)(int)>(0x4C5AD0));
ExternalMethod<CTrain, CTrain*(int modelID, uint8_t createdBy)> CTrainCtor(AddressByVersion<CTrain* (__thiscall*)(CTrain*, int, uint8_t)>(0x6F6030));


// SA variables
void** rwengine = []() -> void** {

		void*** ptr = AddressByVersion<void***>(0x58FFC0);
		if (ptr != nullptr)
		{
			return *ptr;
		}
		assert(!"Could not locate RwEngineInstance!");
		return nullptr;
	}();

static ExternalRef		RsGlobal(AddressByVersion<RsGlobalType**>(0x619602 + 2));

ExternalRef				nGameClockDays(AddressByVersion<uint8_t**>(0x4E841D));
ExternalRef				nGameClockMonths(AddressByVersion<uint8_t**>(0x4E842D));

ExternalRef				pCurrZoneInfo(AddressByVersion<CZoneInfo***>(0x58ADB1));
ExternalRef				HudColour(AddressByVersion<CRGBA (**)[]>(0x58ADF6));

ExternalRef				ms_weaponPedsForPC(AddressByVersion<CLinkListSA<CPed*>**>(0x53EACA));

ExternalRef				bDrawCrossHair(AddressByVersion<uint32_t**>(0x58E7BF + 2));

// Technically part of CMenuManager, but we only need this boolean
static ExternalRef		bIsFrontEndActive(AddressByVersion<bool**>(0x53E9AC + 1));

DebugMenuAPI gDebugMenuAPI;
static bool IgnoresWeaponPedsForPCFix();

// ============= Fixed atomic render functions for blurred rotors/propellers =============
namespace BlurredRotorsAtomicRender
{
	static bool HasGameBindings()
	{
		return EnsureBindings(GetFrameNodeName);
	}

	template<std::size_t Index>
	static RpAtomic* (*orgAtomicDefaultRenderCallback)(RpAtomic* pAtomic);

	template<std::size_t Index>
	static RpAtomic* AtomicDefaultRenderCallBack_HeliRotor(RpAtomic* pAtomic)
	{
		RwScopedRenderState<rwRENDERSTATEALPHATESTFUNCTIONREF> alphaRef;
		RwScopedRenderState<rwRENDERSTATEVERTEXALPHAENABLE> vertexAlpha;

		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, 0);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, reinterpret_cast<void*>(TRUE));
		return orgAtomicDefaultRenderCallback<Index>(pAtomic);
	}

	template<std::size_t Index>
	RpAtomic* AtomicDefaultRenderCallBack_PlaneProp(RpAtomic* pAtomic)
	{
		RwScopedRenderState<rwRENDERSTATEALPHATESTFUNCTIONREF> alphaRef;
		RwScopedRenderState<rwRENDERSTATEVERTEXALPHAENABLE> vertexAlpha;

		if (strstr(GetFrameNodeName.Call(RpAtomicGetFrame(pAtomic)), "_prop") != nullptr)
		{
			RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, 0);
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, reinterpret_cast<void*>(TRUE));
		}

		return orgAtomicDefaultRenderCallback<Index>(pAtomic);
	}

	// Both hooks share orgAtomicDefaultRenderCallback, so give them a counter
	HOOK_EACH_INIT_CTR(HeliRotor, 0, orgAtomicDefaultRenderCallback, AtomicDefaultRenderCallBack_HeliRotor);
	HOOK_EACH_INIT_CTR(PlaneProp, 1, orgAtomicDefaultRenderCallback, AtomicDefaultRenderCallBack_PlaneProp);

	static RpAtomic* (*RenderVehicleHiDetailAlphaCB_BigVehicle)(RpAtomic*);

	static RpAtomic* (*orgSetAtomicRendererCB_BigVehicle)(RpAtomic*, void*);
	static RpAtomic* SetAtomicRendererCB_PlaneOrBigVehicle(RpAtomic* atomic, void* data)
	{
		RpAtomic* result = orgSetAtomicRendererCB_BigVehicle(atomic, data);

		// We do our setup after, not before the game, to override the original decision
		if (strstr(GetFrameNodeName.Call(RpAtomicGetFrame(atomic)), "_prop") != nullptr)
		{
			RpAtomicSetRenderCallBack(atomic, RenderVehicleHiDetailAlphaCB_BigVehicle);
		}

		return result;
	}

	static RpAtomic* (*RenderHeliRotorAlphaCB)(RpAtomic*);
	static RpAtomic* (*RenderHeliTailRotorAlphaCB)(RpAtomic*);

	static RpAtomic* (*orgSetAtomicRendererCB_RealHeli)(RpAtomic*, void*);
	static RpAtomic* SetAtomicRendererCB_RealHeli_StaticRotor(RpAtomic* atomic, void* data)
	{
		RpAtomic* result = orgSetAtomicRendererCB_RealHeli(atomic, data);

		// We do our setup after, not before the game, to override the original decision
		const char* frameName = GetFrameNodeName.Call(RpAtomicGetFrame(atomic));
		if (strcmp(frameName, "static_rotor") == 0)
		{
			RpAtomicSetRenderCallBack(atomic, RenderHeliRotorAlphaCB);
		}
		else if (strcmp(frameName, "static_rotor2") == 0)
		{
			RpAtomicSetRenderCallBack(atomic, RenderHeliTailRotorAlphaCB);
		}

		return result;
	}
}

// ============= Hunter door render flag fix (interior no longer vanishing when looking at it from the right side) =============
namespace HunterDoorRenderFlagFix
{
	static bool HasGameBindings()
	{
		return EnsureBindings(GetAnimationBlockIndex, ClearAtomicFlag, GetFrameNodeName);
	}

	static void (__thiscall *orgPreprocessHierarchy)(CVehicleModelInfo* modelInfo);
	static void __fastcall PreprocessHierarchy_UnmarkHunterDoor(CVehicleModelInfo* modelInfo)
	{
		orgPreprocessHierarchy(modelInfo);

		// We unmark the left front door for all vehicles using Rustler's animations, i.e. door opening to the top
		const int VehicleAnimFileIndex = modelInfo->GetAnimFileIndex();
		if (VehicleAnimFileIndex != -1)
		{
			static const int RustlerAnimFileIndex = GetAnimationBlockIndex.Call("rustler");
			if (VehicleAnimFileIndex == RustlerAnimFileIndex)
			{
				RpClumpForAllAtomics(reinterpret_cast<RpClump*>(modelInfo->pRwObject), [](RpAtomic* atomic) -> RpAtomic*
					{
						if (strncmp(GetFrameNodeName.Call(RpAtomicGetFrame(atomic)), "door_lf", 7) == 0)
						{
							ClearAtomicFlag.Call(atomic, 4); // ATOMIC_IS_LEFT
						}
						return atomic;
					});
			}
		}
	}
}

static bool HasGameBindings_WeaponRenderingFixes()
{
	return EnsureBindings(ms_weaponPedsForPC) && CPed::HasGameBindings_RenderWeapon();
}

void RenderWeapon(CPed* pPed)
{
	if ( !IgnoresWeaponPedsForPCFix() )
	{
		pPed->RenderWeapon(true, false, false);
	}
	ms_weaponPedsForPC.Get().Insert(pPed);
}

void RenderWeaponPedsForPC()
{
	RwScopedRenderState<rwRENDERSTATEVERTEXALPHAENABLE> vertexAlpha;
	RwScopedRenderState<rwRENDERSTATEZWRITEENABLE> zWrite;
	RwScopedRenderState<rwRENDERSTATEFOGENABLE> fogEnable;

	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, reinterpret_cast<void*>(TRUE));
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, reinterpret_cast<void*>(TRUE));
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, reinterpret_cast<void*>(TRUE));

	const bool renderWeapon = IgnoresWeaponPedsForPCFix();

	auto& weaponPedsForPC = ms_weaponPedsForPC.Get();
	for ( auto it = weaponPedsForPC.Next( nullptr ); it != nullptr; it = weaponPedsForPC.Next( it ) )
	{
		CPed* ped = **it;
		const bool bLightingSetup = ped->SetupLighting();
		ped->RenderWeapon(renderWeapon, true, false);
		ped->RemoveLighting(bLightingSetup);
	}
}

namespace UIScales
{
	static float Width_Internal_Multiply(float** factor)
	{
		return RsGlobal.Get().MaximumWidth * **factor;
	}

	static float Height_Internal_Multiply(float** factor)
	{
		return RsGlobal.Get().MaximumHeight * **factor;
	}

	static bool HasGameBindings()
	{
		return EnsureBindings(RsGlobal);
	}

	// CHud - widescreen fixed, currently scaled by HUD scale
	// Currently the same as "Hud", but wsfix may separate it in the future
	struct HudMessages
	{
		static float Width_Multiply()
		{
			static float** Mult = (float**)(0x58D2DB + 2);
			return Width_Internal_Multiply(Mult);
		}

		static float Height_Multiply()
		{
			static float** Mult = (float**)(0x58D2C5 + 2);
			return Height_Internal_Multiply(Mult);
		}

		static inline auto Width = &Width_Multiply;
		static inline auto Height = &Height_Multiply;

	};

	// Render2dStuff - widescreen fixed, unaffected by scaling
	// NOT available in the main menu
	struct Stuff2d
	{
		static float Width_Multiply()
		{
			static float** Mult = (float**)(0x53E472 + 2);
			return Width_Internal_Multiply(Mult);
		}

		static float Height_Multiply()
		{
			static float** Mult = (float**)(0x53E3E7 + 2);
			return Height_Internal_Multiply(Mult);
		}

		static inline auto Width = &Width_Multiply;
		static inline auto Height = &Height_Multiply;

	};

	// CMenuManager - widescreen fixed, unaffected by scaling
	struct MenuManager
	{
		static float Width_Multiply()
		{
			static float** Mult = (float**)(0x5733FD + 2);
			return Width_Internal_Multiply(Mult);
		}

		static float Height_Multiply()
		{
			static float** Mult = (float**)(0x57342D + 2);
			return Height_Internal_Multiply(Mult);
		}

		static inline auto Width = &Width_Multiply;
		static inline auto Height = &Height_Multiply;

	};

	// CFont - widescreen fixed, scaled by HUD size
	struct Font
	{
		static float Width_Multiply()
		{
			static float** Mult = (float**)(0x719C0D + 2);
			return Width_Internal_Multiply(Mult);
		}

		static float Height_Multiply()
		{
			static float** Mult = (float**)(0x719C27 + 2);
			return Height_Internal_Multiply(Mult);
		}

		static inline auto Width = &Width_Multiply;
		static inline auto Height = &Height_Multiply;

	};

	// CReplay
	struct Replay
	{
		static float Width_Multiply()
		{
			static float** Mult = (float**)(0x45C255 + 2);
			return Width_Internal_Multiply(Mult);
		}

		static float Height_Multiply()
		{
			static float** Mult = (float**)(0x45C23F + 2);
			return Height_Internal_Multiply(Mult);
		}

		static inline auto Width = &Width_Multiply;
		static inline auto Height = &Height_Multiply;

	};

}

namespace ScriptFixes
{

static void BasketballFix(unsigned char* pBuf, int nSize)
{
	for ( int i = 0, hits = 0; i < nSize && hits < 7; i++, pBuf++ )
	{
		// Pattern check for save pickup XYZ
		if ( *(unsigned int*)pBuf == 0x449DE19A )		// Save pickup X
		{
			hits++;
			*(float*)pBuf = 1291.8f;
		}
		else if ( *(unsigned int*)pBuf == 0xC4416AE1 )		// Save pickup Y
		{
			hits++;
			*(float*)pBuf = -797.8284f;
		}
		else if ( *(unsigned int*)pBuf == 0x44886C7B )		// Save pickup Z
		{
			hits++;
			*(float*)pBuf = 1089.5f;
		}
		else if ( *(unsigned int*)pBuf == 0x449DF852 )		// Save point X
		{
			hits++;
			*(float*)pBuf = 1286.8f;
		}
		else if ( *(unsigned int*)pBuf == 0xC44225C3 )		// Save point Y
		{
			hits++;
			*(float*)pBuf = -797.69f;
		}
		else if ( *(unsigned int*)pBuf == 0x44885C7B )		// Save point Z
		{
			hits++;
			*(float*)pBuf = 1089.1f;
		}
		else if ( *(unsigned int*)pBuf == 0x43373AE1 )		// Save point A
		{
			hits++;
			*(float*)pBuf = 90.0f;
		}
	}
}

static unsigned char*	ScriptSpace;
static int*				ScriptParams;
static size_t			ScriptFileSize, ScriptMissionSize;

static void InitializeScriptGlobals()
{
	static bool		bInitScriptStuff = [] () {;
		ScriptSpace = *AddressByVersion<unsigned char**>(0x5D5380);
		ScriptParams = *AddressByVersion<int**>(0x48995B);
		ScriptFileSize = *AddressByVersion<size_t*>(0x468E74+1);
		ScriptMissionSize = *AddressByVersion<size_t*>(0x489A5A+1);

		return true;
	} ();
}

static void SweetsGirlFix()
{
	// Changes @ == int to @ >= int in two places
	if ( *(uint16_t*)(ScriptSpace+ScriptFileSize+2510) == 0x0039 )
		*(uint16_t*)(ScriptSpace+ScriptFileSize+2510) = 0x0029;

	if ( *(uint16_t*)(ScriptSpace+ScriptFileSize+2680) == 0x0039 )
		*(uint16_t*)(ScriptSpace+ScriptFileSize+2680) = 0x0029;
}

static hook::scan_segments MakeScriptScanSegment(bool isMission)
{
	uintptr_t begin, end;
	if (isMission)
	{
		begin = uintptr_t(ScriptSpace) + ScriptFileSize;
		end = begin + ScriptMissionSize;
	}
	else
	{
		begin = uintptr_t(ScriptSpace);
		end = begin + ScriptFileSize;
	}
	return {{ begin, end }};
}

static void MountainCloudBoysFix()
{
	auto pattern = hook::pattern(MakeScriptScanSegment(true), "D6 00 04 00 39 00 03 EF 00 04 02 4D 00 01 90 F2 FF FF D6 00 04 01");
	if ( pattern.size() == 1 ) // Faulty code lies under offset 3367 - replace it if it matches
	{
		const uint8_t bNewCode[22] = {
			0x00, 0x00, 0x00, 0x00, 0xD6, 0x00, 0x04, 0x03, 0x39, 0x00, 0x03, 0x2B,
			0x00, 0x04, 0x0B, 0x39, 0x00, 0x03, 0xEF, 0x00, 0x04, 0x02
		};
		memcpy( pattern.get(0).get<void>(), bNewCode, sizeof(bNewCode) );
	}
}

static void IceColdKillaFix()
{
	auto pattern = hook::pattern(MakeScriptScanSegment(true), "06 B9 A6 27 45");
	if (pattern.size() == 1) // Faulty (inverted) float under offet 3255
	{
		float& val = *pattern.get(0).get<float>(1);
		val = -val;
	}
}

static void SupplyLinesFix( bool isBeefyBaron )
{
	auto pattern = hook::pattern(MakeScriptScanSegment(true), isBeefyBaron ? "06 B8 9E 3A 44" : "06 B8 1E 2F 44");
	if ( pattern.size() == 1 ) // 700.48 -> 10.0 (teleports car with CJ under the building instead)
	{
		*pattern.get(0).get<float>(1) = 10.0f;
	}
}

static void DrivingSchoolConesFix()
{
	auto segment = MakeScriptScanSegment(true);
	auto pattern = hook::pattern(segment, "04 00 02 20 03 04 00 D6 00 04 00 1A 00 04 2E 02 20 03 4D 00 01 60 75 FF FF BE 00 08 01 07 24 03 20 03 2E 80 08 00 02 20 03 04 01");
	auto coneCoilConeCount = hook::pattern(segment, "1A 00 04 17 02 20 03");
	auto burnAndLapConeCount = hook::pattern(segment, "1A 00 04 23 02 20 03");
	// Only destroy as many cones as were created, and correct trafficcone_counter for "Cone Coil" and "Burn and Lap"
	if (pattern.size() == 1 && coneCoilConeCount.size() == 1 && burnAndLapConeCount.size() == 1)
	{
		const uint8_t gotoSkipAssignment[] = { 0x02, 0x00, 0x01, 0x8B, 0x75, 0xFF, 0xFF };
		memcpy(pattern.get(0).get<void>(0), gotoSkipAssignment, sizeof(gotoSkipAssignment));

		const uint8_t cmpVal[] = { 0x18, 0x00, 0x02, 0x20, 0x03, 0x04, 0x00 }; // trafficcone_counter > 0
		memcpy(pattern.get(0).get<void>(11), cmpVal, sizeof(cmpVal));

		// trafficcone_counter-- \ DELETE_OBJECT trafficcones[trafficcone_counter]
		const uint8_t subValDeleteObject[] = { 0x0C, 0x00, 0x02, 0x20, 0x03, 0x04, 0x01, 0x08, 0x01, 0x07, 0x24, 0x03, 0x20, 0x03, 0x2E, 0x80 };
		memcpy(pattern.get(0).get<void>(0x1B), subValDeleteObject, sizeof(subValDeleteObject));

		// Also set trafficcone_counter to 0 so the first destruction doesn't happen
		int32_t* trafficcone_counter = reinterpret_cast<int32_t*>(ScriptSpace+800);
		*trafficcone_counter = 0;

		// Correct the final trafficcone_counter in Cone Coil
		// 23 -> 30
		*coneCoilConeCount.get(0).get<int8_t>(3) = 30;


		// Correct the final trafficcone_counter in Burn and Lap
		// 35 -> 42
		*burnAndLapConeCount.get(0).get<int8_t>(3) = 42;
	}
}

static void BikeSchoolConesFix()
{
	auto pattern = hook::pattern(MakeScriptScanSegment(true), "04 00 02 20 03 04 00 D6 00 04 00 1A 00 04 2E 02 20 03 4D 00 01 F8 AD FF FF 08 01 07 24 03 20 03 2E 80 08 00 02 20 03 04 01");
	if (pattern.size() == 1) // Only destroy as many cones as were created
	{
		const uint8_t gotoSkipAssignment[] = { 0x02, 0x00, 0x01, 0x21, 0xAE, 0xFF, 0xFF };
		memcpy(pattern.get(0).get<void>(0), gotoSkipAssignment, sizeof(gotoSkipAssignment));

		const uint8_t cmpVal[] = { 0x18, 0x00, 0x02, 0x20, 0x03, 0x04, 0x00 }; // trafficcone_counter > 0
		memcpy(pattern.get(0).get<void>(11), cmpVal, sizeof(cmpVal));

		// trafficcone_counter-- \ DELETE_OBJECT trafficcones[trafficcone_counter]
		const uint8_t subValDeleteObject[] = { 0x0C, 0x00, 0x02, 0x20, 0x03, 0x04, 0x01, 0x08, 0x01, 0x07, 0x24, 0x03, 0x20, 0x03, 0x2E, 0x80 };
		memcpy(pattern.get(0).get<void>(0x19), subValDeleteObject, sizeof(subValDeleteObject));

		// Also set trafficcone_counter to 0 so the first destruction doesn't happen
		int32_t* trafficcone_counter = reinterpret_cast<int32_t*>(ScriptSpace+800);
		*trafficcone_counter = 0;
	}
}

static void AirRaidFix()
{
	// Give the player back their weapon from the 8th slot instead of stealing it,
	// but do it properly and load the model before giving the wepaon.
	// 1.01 PC script saves and restores the weapon, but forgets to load the model.
	auto segment = MakeScriptScanSegment(true);
	auto save_weapon_gosub_place = hook::pattern(segment, "BD 01 03 65 01 06 00 03 48 01 04 00");
	auto free_space_after_mission = hook::pattern(segment, "EF 04 0E 06 43 41 53 49 4E 4F EF 04 0E 0A 4F 4E 5F 4C 4F 4F 4B 45 52 53 D8 00 51 00");
	if (save_weapon_gosub_place.size() == 1 && free_space_after_mission.size() == 1)
	{
		using namespace Memory;

		// "Assemble" the script bytecode because it's more readable this way
		uintptr_t afterMissionSpace = free_space_after_mission.get(0).get_uintptr(28);

		auto assembleCommand = [](uintptr_t& mem, std::initializer_list<uint8_t> bytes)
			{
				uint8_t* buf = reinterpret_cast<uint8_t*>(mem);
				std::copy(bytes.begin(), bytes.end(), buf);
				mem += bytes.size();
			};

		auto assembleMissionOffset = [](uintptr_t& mem, uintptr_t dest)
			{
				const int32_t offset = (uintptr_t(ScriptSpace) + ScriptFileSize) - dest;
				memcpy(reinterpret_cast<uint8_t*>(mem), &offset, sizeof(offset));
				mem += sizeof(offset);
			};

		auto assembleInt32 = [](uintptr_t& mem, int32_t val)
			{
				memcpy(reinterpret_cast<uint8_t*>(mem), &val, sizeof(val));
				mem += sizeof(val);
			};

		// Store the weapon
		{
			// GOSUB zero1_store_weapon
			uintptr_t missionInitSpace = save_weapon_gosub_place.get(0).get_uintptr(5);
			assembleCommand(missionInitSpace, { 0x50, 0x00, 0x01 });
			assembleMissionOffset(missionInitSpace, afterMissionSpace);

			// zero1_store_weapon:
			assembleCommand(afterMissionSpace, { 0x06, 0x00, 0x03, 0x48, 0x01, 0x04, 0x00 }); // index_zero1 = 0
			assembleCommand(afterMissionSpace, { 0xB8, 0x04, 0x02, 0x0C, 0x00, 0x04, 0x08, 0x03, 0x72, 0x01, 0x03, 0x73, 0x01, 0x03, 0x74, 0x01}); // GET_CHAR_WEAPON_IN_SLOT scplayer 8 weapontype_zero1 ammo_zero1 model_for_weapon_zero1
			assembleCommand(afterMissionSpace, { 0x51, 0x00 }); // RETURN
		}

		// Restore the weapon
		{
			uintptr_t missionCleanupGosub = uintptr_t(ScriptSpace) + ScriptFileSize + 0x1E;
			const int32_t originalMissionCleanup = *reinterpret_cast<int32_t*>(missionCleanupGosub);
			assembleMissionOffset(missionCleanupGosub, afterMissionSpace);

			assembleCommand(afterMissionSpace, { 0x12, 0x81 }); // NOT HAS_DEATHARREST_BEEN_EXECUTED
			assembleCommand(afterMissionSpace, { 0x4D, 0x00, 0x01 }); // GOTO_IF_FALSE originalMissionCleanup
			// We can jupm directly back to the original mission cleanup to simplify code generation
			assembleInt32(afterMissionSpace, originalMissionCleanup);

			// The original fix from 1.01/2.0 PC versions only added GIVE_WEAPON_TO_CHAR, without loading the model
			// We fix it properly by ensuring the model is loaded before giving the weapon
			assembleCommand(afterMissionSpace, { 0x47, 0x02, 0x03, 0x74, 0x01 }); // REQUEST_MODEL model_for_weapon_zero1
			assembleCommand(afterMissionSpace, { 0x8B, 0x03 }); // LOAD_ALL_MODELS_NOW
			assembleCommand(afterMissionSpace, { 0xB2, 0x01, 0x02, 0x0C, 0x00, 0x03, 0x72, 0x01, 0x03, 0x73, 0x01 });  // GIVE_WEAPON_TO_CHAR scplayer weapontype_zero1 ammo_zero1
			assembleCommand(afterMissionSpace, { 0x49, 0x02, 0x03, 0x74, 0x01 }); // MARK_MODEL_AS_NO_LONGER_NEEDED model_for_weapon_zero1

			assembleCommand(afterMissionSpace, { 0x02, 0x00, 0x01 }); // GOTO originalMissionCleanup
			assembleInt32(afterMissionSpace, originalMissionCleanup);
		}
	}
}

static void QuadrupleStuntBonus()
{
	// IF HEIGHT_FLOAT_HJ > 4.0 -> IF HEIGHT_INT_HJ > 4
	auto pattern = hook::pattern(MakeScriptScanSegment(false), "20 00 02 60 14 06 00 00 80 40");
	if ( pattern.size() == 1 )
	{
		const uint8_t newCode[10] = {
			0x18, 0x00, 0x02, 0x30, 0x14, 0x01, 0x04, 0x00, 0x00, 0x00
		};
		memcpy( pattern.get(0).get<void>(), newCode, sizeof(newCode) );
	}
}

static void	(*orgTheScriptsLoad)();
void TheScriptsLoad_BasketballFix()
{
	orgTheScriptsLoad();
	InitializeScriptGlobals();

	BasketballFix(ScriptSpace+8, *(int*)(ScriptSpace+3));
	QuadrupleStuntBonus();
}

static void StartNewMission_SCMFixes()
{
	InitializeScriptGlobals();

	const int missionID = ScriptParams[0];
	switch (missionID)
	{
	// INITIAL - Basketball fix, Quadruple Stunt Bonus
	case 0:
		BasketballFix(ScriptSpace+ScriptFileSize, ScriptMissionSize);
		QuadrupleStuntBonus();
		break;
	// HOODS5 - Sweet's Girl fix
	case 18:
		SweetsGirlFix();
		break;
	// WUZI1 - Mountain Cloud Boys fix
	case 53:
		MountainCloudBoysFix();
		break;
	// SYND4 - Ice Cold Killa fix
	case 61:
		IceColdKillaFix();
		break;
	// DSKOOL - Driving School cones fix
	// By Wesser
	case 71:
		DrivingSchoolConesFix();
		break;
	// BSKOOL - Bike School cones fix
	// By Wesser
	case 120:
		BikeSchoolConesFix();
		break;
	// ZERO1 - Air Raid fix
	case 72:
		AirRaidFix();
		break;
	// ZERO2 - Supply Lines fix
	case 73:
		SupplyLinesFix( false );
		break;
	// ZERO5 - Beefy Baron fix
	case 10:
		SupplyLinesFix( true );
		break;
	default:
		break;
	}
}

template<std::size_t Index>
static void (*orgWipeLocalVariableMemoryForMissionScript)();

template<std::size_t Index>
static void WipeLocalVariableMemoryForMissionScript_ApplyFixes()
{
	orgWipeLocalVariableMemoryForMissionScript<Index>();
	StartNewMission_SCMFixes();
}

HOOK_EACH_INIT(SCMFixes, orgWipeLocalVariableMemoryForMissionScript, WipeLocalVariableMemoryForMissionScript_ApplyFixes)

}

// 1.01 kinda fixed it
bool GetCurrentZoneLockedOrUnlocked(float fPosX, float fPosY)
{
	// Exploit RAII really bad
	static const float		GridXOffset = **(float**)(0x572135+2), GridYOffset = **(float**)(0x57214A+2);
	static const float		GridXSize = **(float**)(0x57213B+2), GridYSize = **(float**)(0x572153+2);
	static const int		GridXNum = static_cast<int>((2.0f*GridXOffset) * GridXSize), GridYNum = static_cast<int>((2.0f*GridYOffset) * GridYSize);

	static unsigned char* const	ZonesVisited = *(unsigned char**)(0x57216A) - (GridYNum-1);		// 1.01 fixed it!

	int		Xindex = static_cast<int>((fPosX+GridXOffset) * GridXSize);
	int		Yindex = static_cast<int>((fPosY+GridYOffset) * GridYSize);

	// "Territories fix"
	if ( (Xindex >= 0 && Xindex < GridXNum) && (Yindex >= 0 && Yindex < GridYNum) )
		return ZonesVisited[GridXNum*Xindex - Yindex + (GridYNum-1)] != 0;

	// Outside of map bounds
	return true;
}

namespace ColouredZoneNames
{
	static bool HasGameBindings()
	{
		return EnsureBindings(pCurrZoneInfo, HudColour);
	}

	CRGBA* __fastcall BlendGangColour(CRGBA* pThis, void*, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
	{
		const double colourIntensity = std::min( static_cast<double>(pCurrZoneInfo.Get()->ZoneColour.a) / 120.0, 1.0 );
		*pThis = CRGBA(BlendSqr( HudColour.Get()[3], CRGBA(r, g, b), colourIntensity ), a);
		return pThis;
	}

	static bool bColouredZoneNames;
	CRGBA* __fastcall BlendGangColour_Dynamic(CRGBA* pThis, void*, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
	{
		if ( bColouredZoneNames )
		{
			return BlendGangColour(pThis, nullptr, r, g, b, a);
		}
		*pThis = CRGBA(HudColour.Get()[3], a);
		return pThis;
	}
}

static ExternalRef ppUserFilesDir(AddressByVersion<const char (**)[]>(0x74503F));
char* GetMyDocumentsPathSA()
{
	static char* const pDocumentsPath = [&] () -> char* {
		char* const ppTempBufPtr = *AddressByRegion_10<char**>(0x744FE5);

		if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_MYDOCUMENTS, nullptr, SHGFP_TYPE_CURRENT, ppTempBufPtr)))
		{
			PathAppendA(ppTempBufPtr, ppUserFilesDir.Get());
			CreateDirectoryA(ppTempBufPtr, nullptr);
		}
		else
		{
			strcpy_s(ppTempBufPtr, MAX_PATH, "data");
		}

		char cTmpPath[MAX_PATH];

		strcpy_s(cTmpPath, ppTempBufPtr);
		PathAppendA(cTmpPath, "Gallery");
		CreateDirectoryA(cTmpPath, nullptr);

		strcpy_s(cTmpPath, ppTempBufPtr);
		PathAppendA(cTmpPath, "User Tracks");
		CreateDirectoryA(cTmpPath, nullptr);

		return ppTempBufPtr;
	} ();
	return pDocumentsPath;
}

static LARGE_INTEGER	FrameTime;
__declspec(safebuffers) int32_t GetTimeSinceLastFrame()
{
	LARGE_INTEGER	curTime;
	QueryPerformanceCounter(&curTime);
	return int32_t(curTime.QuadPart - FrameTime.QuadPart);
}

static int (*RsEventHandler)(int, void*);
int NewFrameRender(int nEvent, void* pParam)
{
	QueryPerformanceCounter(&FrameTime);
	return RsEventHandler(nEvent, pParam);
}

namespace LensFlareRenderFix
{
	static ExternalFunc FlushSpriteBuffer(AddressByVersion<void(*)()>(0x70CF20));
	static ExternalFunc InitSpriteBuffer2D(AddressByVersion<void(*)()>(0x70CFD0));

	static bool HasGameBindings()
	{
		return EnsureBindings(FlushSpriteBuffer, InitSpriteBuffer2D);
	}

	void FlushLensSwitchZ( RwRenderState rwa, void* rwb )
	{
		FlushSpriteBuffer.Call();
		RwRenderStateSet( rwa, rwb );
	}

	void InitBufferSwitchZ( RwRenderState rwa, void* rwb )
	{
		RwRenderStateSet( rwa, rwb );
		InitSpriteBuffer2D.Call();
	}
}

static ExternalRef g_fx(AddressByVersion<void***>(0x4A9649));
static int32_t GetFxQuality()
{
	void* fx = g_fx.Address();
	return *(int32_t*)( (uint8_t*)fx + 0x54 );
}


namespace MirrorsDepthFix
{

static ExternalRef s_msaaValues(AddressByVersion<DWORD (**)[2]>(0x4CCBC5));
// These patterns have 3 hits, but that's fine as all 3 refer to exact same variables
static ExternalRef s_pMirrorBuffer(AddressByVersion<RwRaster***>(0x723001));
static ExternalRef s_pMirrorZBuffer(AddressByVersion<RwRaster***>(0x72301C));

static bool HasGameBindings()
{
	return RWBindings::RwRasterCreate() && EnsureBindings(s_msaaValues, s_pMirrorBuffer, s_pMirrorZBuffer, g_fx);
}

static void CreateMirrorBuffers()
{
	auto& pMirrorBuffer = s_pMirrorBuffer.Get();
	if ( pMirrorBuffer == nullptr )
	{
		auto& msaaValues = s_msaaValues.Get();
		DWORD oldMsaa[2] = { msaaValues[0], msaaValues[1] };
		msaaValues[0] = msaaValues[1] = 0;

		int32_t quality = GetFxQuality();
		RwInt32 width, height;

		if ( quality >= 3 ) // Very High
		{
			width = 2048;
			height = 1024;
		}
		else if ( quality >= 1 ) // Medium
		{
			width = 1024;
			height = 512;
		}
		else
		{
			width = 512;
			height = 256;
		}

		pMirrorBuffer = RwRasterCreate( width, height, 0, rwRASTERTYPECAMERATEXTURE );
		s_pMirrorZBuffer.Get() = RwRasterCreate( width, height, 0, rwRASTERTYPEZBUFFER );

		msaaValues[0] = oldMsaa[0];
		msaaValues[1] = oldMsaa[1];
	}
}

}

namespace MSAAFixes
{

static RwUInt32 GetMaxMultiSamplingLevels_BitScan(RwUInt32 maxSamples)
{
	RwUInt32 option;
	_BitScanForward( (DWORD*)&option, maxSamples );
	return option + 1;
}

template<typename std::size_t Index>
static RwUInt32 (*orgGetMaxMultiSamplingLevels)();

template<typename std::size_t Index>
static RwUInt32 GetMaxMultiSamplingLevels()
{
	return GetMaxMultiSamplingLevels_BitScan(orgGetMaxMultiSamplingLevels<Index>());
}
HOOK_EACH_INIT(GetMaxMultiSamplingLevels, orgGetMaxMultiSamplingLevels, GetMaxMultiSamplingLevels);

template<typename std::size_t Index>
static void (*orgSetOrChangeMultiSamplingLevels)(RwUInt32);

template<typename std::size_t Index>
static void SetOrChangeMultiSamplingLevels(RwUInt32 level)
{
	orgSetOrChangeMultiSamplingLevels<Index>( 1 << (level - 1) );
}
HOOK_EACH_INIT(SetOrChangeMultiSamplingLevels, orgSetOrChangeMultiSamplingLevels, SetOrChangeMultiSamplingLevels);

void MSAAText( char* buffer, const char*, DWORD level )
{
	sprintf_s( buffer, 100, "%ux", 1 << level );
}

}


static RwInt32 numSavedVideoModes;
static RwInt32 (*orgGetNumVideoModes)();
RwInt32 GetNumVideoModes_Store()
{
	return numSavedVideoModes = orgGetNumVideoModes();
}

RwInt32 GetNumVideoModes_Retrieve()
{
	return numSavedVideoModes;
}

namespace UnitializedCollisionDataFix
{

static void* (*orgMemMgrMalloc)(RwUInt32, RwUInt32);
static void* CollisionData_MallocAndInit( RwUInt32 size, RwUInt32 hint )
{
	CColData*	mem = (CColData*)orgMemMgrMalloc( size, hint );

	mem->m_bFlags = 0;
	mem->m_dwNumShadowTriangles = mem->m_dwNumShadowVertices = 0;
	mem->m_pShadowVertices = mem->m_pShadowTriangles = nullptr;

	return mem;
}

template<std::size_t Index>
static void* (*orgNewAlloc)(size_t);

template<std::size_t Index>
static void* CollisionData_NewAndInit(size_t size)
{
	CColData*	mem = (CColData*)orgNewAlloc<Index>(size);

	mem->m_bFlags = 0;

	return mem;
}
HOOK_EACH_INIT(CollisionDataNew, orgNewAlloc, CollisionData_NewAndInit);

}

static void (*orgEscalatorsUpdate)();
void UpdateEscalators()
{
	if ( !CEscalator::ms_entitiesToRemove.empty() )
	{
		for ( auto it : CEscalator::ms_entitiesToRemove )
		{
			WorldRemove.Call( it );
			delete it;
		}
		CEscalator::ms_entitiesToRemove.clear();
	}
	orgEscalatorsUpdate();
}


static ExternalRef s_pStencilShadowsPad(AddressByVersion<char* (**)[3]>(0x70FC4F));
void StencilShadowAlloc( )
{
	static char* pMemory = [] () {;
		char* mem = static_cast<char*>( ::operator new( 3 * 0x6000 ) );

		auto& pStencilShadowsPad = s_pStencilShadowsPad.Get();
		pStencilShadowsPad[0] = mem;
		pStencilShadowsPad[1] = mem+0x6000;
		pStencilShadowsPad[2] = mem+(2*0x6000);

		return mem;
	} ();
}

RwBool GTARtAnimInterpolatorSetCurrentAnim(RtAnimInterpolator* animI, RtAnimAnimation* anim)
{
	animI->pCurrentAnim = anim;
	animI->currentTime = 0.0f;

	const RtAnimInterpolatorInfo* info = anim->interpInfo;
	animI->currentInterpKeyFrameSize = info->interpKeyFrameSize;
	animI->currentAnimKeyFrameSize = info->animKeyFrameSize;
	animI->keyFrameApplyCB = info->keyFrameApplyCB;
	animI->keyFrameBlendCB = info->keyFrameBlendCB;
	animI->keyFrameInterpolateCB = info->keyFrameInterpolateCB;
	animI->keyFrameAddCB = info->keyFrameAddCB;

	for ( RwInt32 i = 0; i < animI->numNodes; ++i )
	{
		RtAnimKeyFrameInterpolate( animI, rtANIMGETINTERPFRAME( animI, i ),
			(RwChar*)anim->pFrames + i * animI->currentAnimKeyFrameSize,
			(RwChar*)anim->pFrames + ( i + animI->numNodes) * animI->currentAnimKeyFrameSize, 0.0f );
	}

	animI->pNextFrame = (RwChar*)anim->pFrames + 2 * animI->currentAnimKeyFrameSize * animI->numNodes;

	return TRUE;
}

DWORD WINAPI CdStreamSetFilePointer( HANDLE hFile, uint32_t distanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod )
{
	assert( lpDistanceToMoveHigh == nullptr );

	LARGE_INTEGER li;
	li.QuadPart = int64_t(distanceToMove) << 11;
	return SetFilePointer( hFile, li.LowPart, &li.HighPart, dwMoveMethod );
}
static auto* const pCdStreamSetFilePointer = CdStreamSetFilePointer;

static void (*orgDrawScriptSpritesAndRectangles)(void* arg);
void DrawScriptSpritesAndRectangles(void* arg)
{
	RwScopedRenderState<rwRENDERSTATETEXTUREFILTER> state;

	RwRenderStateSet( rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR );
	orgDrawScriptSpritesAndRectangles(arg);
}

// Now in VehicleSA.cpp
bool ReadDoubleRearWheels(const wchar_t* pPath);
bool __stdcall CheckDoubleRWheelsList( void* modelInfo, uint8_t* handlingData );

CVehicleModelInfo* (__thiscall *orgVehicleModelInfoInit)(CVehicleModelInfo*);
CVehicleModelInfo* __fastcall VehicleModelInfoInit(CVehicleModelInfo* me)
{
	orgVehicleModelInfoInit(me);

	// Hack to satisfy some null checks
	static uintptr_t DUMMY;
	me->__removedInSilentPatch = &DUMMY;

	std::fill(std::begin(me->m_firstColour), std::end(me->m_firstColour), nullptr);
	std::fill(std::begin(me->m_secondColour), std::end(me->m_secondColour), nullptr);
	std::fill(std::begin(me->m_thirdColour), std::end(me->m_thirdColour), nullptr);
	std::fill(std::begin(me->m_fourthColour), std::end(me->m_fourthColour), nullptr);

	me->m_dirtMaterials = nullptr;
	me->m_numDirtMaterials = 0;
	std::fill( std::begin( me->m_staticDirtMaterials ), std::end( me->m_staticDirtMaterials ), nullptr );
	return me;
}

static ExternalFunc RemoveFromInterestingVehicleList(AddressByVersion<void(*)(CVehicle*)>(0x423ED0));
static void (*orgRecordVehicleDeleted)(CVehicle*);
static void RecordVehicleDeleted_AndRemoveFromVehicleList( CVehicle* vehicle )
{
	orgRecordVehicleDeleted( vehicle );
	RemoveFromInterestingVehicleList.Call( vehicle );
}

namespace LoopDisplayedSplashes
{
	static ExternalRef s_currDisplayedSplash(AddressByVersion<int**>(0x590B22 + 1));

	static ExternalRef s_splashesBegin(AddressByVersion<RwTexture* (**)[]>(0x590CB4 + 1));
	static ExternalRef s_splashesEnd(AddressByVersion<RwTexture* (**)[]>(0x590CCE + 2));

	static bool HasGameBindings()
	{
		return EnsureBindings(s_currDisplayedSplash, s_splashesBegin, s_splashesEnd);
	}

	static int currDisplayedSplash_ForLastSplash = 0;
	static void DoPCScreenChange_Mod()
	{
		static const int numSplashes = std::distance( s_splashesBegin.Get(), s_splashesEnd.Get() ) - 1;

		auto& currDisplayedSplash = s_currDisplayedSplash.Get();
		if ( currDisplayedSplash >= numSplashes )
		{
			currDisplayedSplash = 1;
			currDisplayedSplash_ForLastSplash = numSplashes + 1;
		}
		else
		{
			currDisplayedSplash_ForLastSplash = ++currDisplayedSplash;
		}
	}
}

namespace DirectionalFromSun
{
	static ExternalRef VectorToSun(AddressByVersion<CVector (**)[]>(0x6FC5B7 + 3));
	static ExternalRef CurrentStoredValue(AddressByVersion<int**>(0x6FC632 + 1));
	static ExternalRef vecDirnLightToSun(AddressByVersion<CVector**>(0x5BC040 + 2));
	static bool HasGameBindings()
	{
		return EnsureBindings(VectorToSun, CurrentStoredValue, vecDirnLightToSun);
	}

	static bool bUseAaronSun;
	static CVector curVecToSun;
	static void (*orgSetLightsWithTimeOfDayColour)( RpWorld* );
	static void SetLightsWithTimeOfDayColour_SilentPatch( RpWorld* world )
	{
		curVecToSun = bUseAaronSun ? VectorToSun.Get()[CurrentStoredValue.Get()] : vecDirnLightToSun.Get();
		orgSetLightsWithTimeOfDayColour( world );
	}
}

// ============= CdStream data racing issue =============

struct CdStream
{
	DWORD nSectorOffset;
	DWORD nSectorsToRead;
	LPVOID lpBuffer;
	BYTE field_C;
	BYTE bLocked;
	BYTE bInUse;
	BYTE field_F;
	DWORD status;
	union Sync {
		HANDLE semaphore;
		CONDITION_VARIABLE cv;
	} sync;
	HANDLE hFile;
	OVERLAPPED overlapped;
};

static_assert(sizeof(CdStream) == 0x30, "Incorrect struct size: CdStream");

namespace CdStreamSync {

static CRITICAL_SECTION CdStreamCritSec;

// WRL-like lock object
// Balancing lock/unlock is up to the user!
class SyncLock
{
public:
	SyncLock( CRITICAL_SECTION& critSec )
		: m_critSec( critSec )
	{
		Lock();
	}

	~SyncLock()
	{
		Unlock();
	}

	void Lock() const { EnterCriticalSection( &m_critSec ); }
	void Unlock() const { LeaveCriticalSection( &m_critSec ); }

	CRITICAL_SECTION* Get() const { return &m_critSec; }

private:
	SyncLock( const SyncLock& ) = delete;
	SyncLock( SyncLock&& ) = delete;
	SyncLock& operator=( const SyncLock& ) = delete;
	SyncLock& operator=( SyncLock&& ) = delete;

	CRITICAL_SECTION& m_critSec;
};



// Function pointers for game to use
static CdStream::Sync (__stdcall *CdStreamInitializeSyncObject)();
static DWORD (__stdcall *CdStreamSyncOnObject)( CdStream* stream );
static void (__stdcall *CdStreamThreadOnObject)( CdStream* stream );
static void (__stdcall *CdStreamCloseObject)( CdStream::Sync* sync );
static void (__stdcall *CdStreamShutdownSyncObject)( CdStream* stream );

static void __stdcall CdStreamShutdownSyncObject_Stub( CdStream* stream, size_t idx )
{
	CdStreamShutdownSyncObject( &stream[idx] );
}

// Fixed return values for GetOverlappedResult - stock code assumes "nonzero" equals 1, might not be future proof
static uint32_t WINAPI GetOverlappedResult_SilentPatch( HANDLE hFile, LPOVERLAPPED lpOverlapped, LPDWORD lpNumberOfBytesTransferred, BOOL bWait )
{
	return GetOverlappedResult( hFile, lpOverlapped, lpNumberOfBytesTransferred, bWait ) != FALSE ? 0 : 254;
}
static auto* const pGetOverlappedResult = &GetOverlappedResult_SilentPatch;


namespace Sema
{
	CdStream::Sync __stdcall InitializeSyncObject()
	{
		CdStream::Sync object;
		object.semaphore = CreateSemaphore( nullptr, 0, 2, nullptr );
		return object;
	}

	void __stdcall ShutdownSyncObject( CdStream* stream )
	{
		CloseHandle( stream->sync.semaphore );
	}

	DWORD __stdcall CdStreamSync( CdStream* stream )
	{
		auto lock = SyncLock( CdStreamCritSec );
		if ( stream->nSectorsToRead != 0 )
		{
			stream->bLocked = 1;
			lock.Unlock();
			WaitForSingleObject( stream->sync.semaphore, INFINITE );
			lock.Lock();
		}
		stream->bInUse = 0;
		return stream->status;
	}

	void __stdcall CdStreamThread( CdStream* stream )
	{
		auto lock = SyncLock( CdStreamCritSec );
		stream->nSectorsToRead = 0;
		if ( stream->bLocked != 0 )
		{
			ReleaseSemaphore( stream->sync.semaphore, 1, nullptr );
		}
		stream->bInUse = 0;
	}
}

namespace CV
{
	namespace Funcs
	{
		static decltype(InitializeConditionVariable)* pInitializeConditionVariable = nullptr;
		static decltype(SleepConditionVariableCS)* pSleepConditionVariableCS = nullptr;
		static decltype(WakeConditionVariable)* pWakeConditionVariable = nullptr;

		static bool TryInit()
		{
			const HMODULE kernelDLL = GetModuleHandle( TEXT("kernel32") );
			assert( kernelDLL != nullptr );
			pInitializeConditionVariable = (decltype(pInitializeConditionVariable))GetProcAddress( kernelDLL, "InitializeConditionVariable" );
			pSleepConditionVariableCS = (decltype(pSleepConditionVariableCS))GetProcAddress( kernelDLL, "SleepConditionVariableCS" );
			pWakeConditionVariable = (decltype(pWakeConditionVariable))GetProcAddress( kernelDLL, "WakeConditionVariable" );

			return pInitializeConditionVariable != nullptr && pSleepConditionVariableCS != nullptr && pWakeConditionVariable != nullptr;
		}


	}

	CdStream::Sync __stdcall InitializeSyncObject()
	{
		CdStream::Sync object;
		Funcs::pInitializeConditionVariable( &object.cv );
		return object;
	}

	void __stdcall ShutdownSyncObject( CdStream* stream )
	{
	}

	DWORD __stdcall CdStreamSync( CdStream* stream )
	{
		auto lock = SyncLock( CdStreamCritSec );
		while ( stream->nSectorsToRead != 0 )
		{
			Funcs::pSleepConditionVariableCS( &stream->sync.cv, lock.Get(), INFINITE );
		}
		stream->bInUse = 0;
		return stream->status;
	}

	void __stdcall CdStreamThread( CdStream* stream )
	{
		auto lock = SyncLock( CdStreamCritSec );
		stream->nSectorsToRead = 0;
		Funcs::pWakeConditionVariable( &stream->sync.cv );
		stream->bInUse = 0;
	}
}

static void (*orgCdStreamInitThread)();
static void CdStreamInitThread()
{
	if ( ModCompat::bCdStreamFallBackForOldML != true && CV::Funcs::TryInit() )
	{
		CdStreamInitializeSyncObject = CV::InitializeSyncObject;
		CdStreamShutdownSyncObject = CV::ShutdownSyncObject;
		CdStreamSyncOnObject = CV::CdStreamSync;
		CdStreamThreadOnObject = CV::CdStreamThread;
	}
	else
	{
		CdStreamInitializeSyncObject = Sema::InitializeSyncObject;
		CdStreamShutdownSyncObject = Sema::ShutdownSyncObject;
		CdStreamSyncOnObject = Sema::CdStreamSync;
		CdStreamThreadOnObject = Sema::CdStreamThread;
	}

	InitializeCriticalSection( &CdStreamCritSec );

	FLAUtils::SetCdStreamWakeFunction( []( CdStream* pStream ) {
		CdStreamThreadOnObject( pStream );
	} );

	orgCdStreamInitThread();
}

}

// Dancing timers fix
static LARGE_INTEGER UtilsStartTime;
static LARGE_INTEGER UtilsFrequency;
static BOOL WINAPI AudioUtilsFrequency( PLARGE_INTEGER lpFrequency )
{
	lpFrequency->QuadPart = UtilsFrequency.QuadPart;
	return TRUE;
}
static auto* const pAudioUtilsFrequency = &AudioUtilsFrequency;

static int64_t AudioUtilsGetStartTime()
{
	QueryPerformanceCounter( &UtilsStartTime );
	return UtilsStartTime.QuadPart;
}

static int64_t AudioUtilsGetCurrentTimeInMs()
{
	LARGE_INTEGER currentTime;
	QueryPerformanceCounter( &currentTime );
	return ((currentTime.QuadPart - UtilsStartTime.QuadPart) * 1000) / UtilsFrequency.QuadPart;
}

// ============= Minimal HUD changes =============
namespace MinimalHUD
{
	static CRGBA* __fastcall SetRGBA_FloatAlpha( CRGBA* rgba, void*, uint8_t red, uint8_t green, uint8_t blue, float alpha )
	{
		rgba->r = red;
		rgba->g = green;
		rgba->b = blue;
		rgba->a = static_cast<uint8_t>(alpha);
		return rgba;
	}

	static void (*orgRenderOneXLUSprite)(float, float, float, float, float, uint8_t, uint8_t, uint8_t, int16_t, float, uint8_t, uint8_t, uint8_t);
	static void RenderXLUSprite_FloatAlpha( float arg1, float arg2, float arg3, float arg4, float arg5, uint8_t red, uint8_t green, uint8_t blue, int16_t mult, float arg10, float alpha, uint8_t arg12, uint8_t arg13 )
	{
		orgRenderOneXLUSprite( arg1, arg2, arg3, arg4, arg5, red, green, blue, mult, arg10, static_cast<uint8_t>(alpha), arg12, arg13 );
	}
}

// 6 directionals on Medium/High/Very High Visual FX
int32_t GetMaxExtraDirectionals( uint32_t areaID )
{
	return areaID != 0 || GetFxQuality() >= 1 ? 6 : 4;
}

static CVehicle* FindPlayerVehicle_RCWrap( int playerID, bool )
{
	return FindPlayerVehicle( playerID, true );
}


// ============= Bicycle fire fix =============
namespace BicycleFire
{
	static CPed* GetVehicleDriver( const CVehicle* vehicle )
	{
		return vehicle->GetDriver();
	}

	static void __fastcall DoStuffToGoOnFire_Noop( void* ped )
	{
	}

	static void (__thiscall* orgStartFire)(CFireManager* fireManager, CEntity* entity, void* attacker, void* a3, void* a4, void* a5, void* a6);
	static void __fastcall StartFire_NullEntityCheck(CFireManager* fireManager, void*, CEntity* entity, void* attacker, void* a3, void* a4, void* a5, void* a6)
	{
		if (entity != nullptr)
		{
			orgStartFire(fireManager, entity, attacker, a3, a4, a5, a6);
		}
	}
}


// ============= Keyboard latency input fix =============
namespace KeyboardInputFix
{
	static void* NewKeyState;
	static void* OldKeyState;
	static void* TempKeyState;
	static size_t objSize;
	static void (__fastcall *orgClearSimButtonPressCheckers)(void*);
	void __fastcall ClearSimButtonPressCheckers(void* pThis)
	{
		memcpy( OldKeyState, NewKeyState, objSize );
		memcpy( NewKeyState, TempKeyState, objSize );

		orgClearSimButtonPressCheckers(pThis);
	}
}

// ============= handling.cfg name matching fix =============
namespace HandlingNameLoadFix
{
	void strncpy_Fix( const char** destination, const char* source, size_t )
	{
		*destination = source;
	}

	int strncmp_Fix( const char* str1, const char** str2, size_t )
	{
		return strcmp( str1, *str2 );
	}
};


// ============= Firela ladder animation =============
namespace FirelaHook
{
	static uintptr_t UpdateMovingCollisionJmp;
	static uintptr_t HydraulicControlJmpBack;

	__declspec(naked) static void TestFirelaAndFlags()
	{
		_asm
		{
			push	ecx		// Required in 0x6B1FE4: test cl, cl
			mov		ecx, esi
			call	CVehicle::HasFirelaLadder
			pop		ecx
			test	al, al
			jnz		TestFirelaAndFlags_UpdateMovingCollision
			test	[esi].hFlagsLocal, FLAG_HYDRAULICS_INSTALLED
			jmp		HydraulicControlJmpBack

		TestFirelaAndFlags_UpdateMovingCollision:
			jmp		UpdateMovingCollisionJmp
		}
	}

	static uintptr_t FollowCarCamNoMovement;
	static uintptr_t FollowCarCamJmpBack;

	__declspec(naked) static void CamControlFirela()
	{
		_asm
		{
			mov		ecx, edi
			call	CVehicle::HasFirelaLadder
			test	al, al
			jnz		TestFirelaAndFlags_UpdateMovingCollision
			mov		eax, [edi].m_dwVehicleClass
			jmp		FollowCarCamJmpBack

		TestFirelaAndFlags_UpdateMovingCollision:
			jmp		FollowCarCamNoMovement
		}
	}
}

namespace HierarchyTypoFix
{
	// Allow wheel_lm vs wheel_lm_dummy and miscX vs misc_X typos
	// Must be sorted by the second parameter
	static constexpr std::pair<const char*, const char*> typosAndFixes[] = {
		{ "boat_moving_hi", "boat_moving" },
		{ "elevator_r", "elevator" },
		{ "misc_a", "misca" },
		{ "misc_b", "miscb" },
		{ "taillights", "tailights" },
		{ "taillights2", "tailights2" },
		{ "transmission_f", "transmision_f" },
		{ "transmission_r", "transmision_r" },
		{ "wheel_lm_dummy", "wheel_lm" },
	};
	static int (*orgStrcasecmp)(const char*, const char*);
	int strcasecmp( const char* dataName, const char* nodeName )
	{
		/*assert( std::is_sorted(std::begin(typosAndFixes), std::end(typosAndFixes), [] (const auto& a, const auto& b) {
			return _stricmp( a.second, b.second ) < 0;
		}) );*/

		const int origComp = orgStrcasecmp( dataName, nodeName );
		if ( origComp == 0 ) return 0;

		for ( const auto& typo : typosAndFixes )
		{
			const int nodeComp = _stricmp( typo.second, nodeName );
			if ( nodeComp > 0 ) break;

			if ( nodeComp == 0 && _stricmp( typo.first, dataName ) == 0 )
			{
				return 0;
			}
		}

		return origComp;
	}

}


// Resetting stats and variables on New Game
namespace VariableResets
{
	// Custom reset values for variables
	template<typename T>
	struct TimeNextMadDriverChaseCreated_t
	{
		T m_timer;

		TimeNextMadDriverChaseCreated_t()
			: m_timer( (static_cast<float>(ConsoleRandomness::rand31()) / INT32_MAX) * 600.0f + 600.0f )
		{
		}
	};

	template<typename T, T val>
	struct ResetToValue_t
	{
		T m_value = val;
	};

	using ResetToTrue_t = ResetToValue_t<bool, true>;

	using VarVariant = std::variant< ExternalRef<bool>, ExternalRef<int>, ExternalRef<TimeNextMadDriverChaseCreated_t<float>>, ExternalRef<ResetToTrue_t> >;
	static std::vector<VarVariant> GameVariablesToReset;
	static std::vector<std::string> PickupDefs, CarGeneratorDefs, StuntJumpDefs;

	template<typename T>
	static void AddVariableToReset(T** ptr)
	{
		GameVariablesToReset.emplace_back(std::in_place_type<ExternalRef<T>>, ptr);
	}

	template<typename T>
	static void AddVariableToReset(std::string_view pattern, std::ptrdiff_t offset = 0) try
	{
		GameVariablesToReset.emplace_back(std::in_place_type<ExternalRef<T>>, hook::txn::get_pattern<T*>(pattern, offset));
	}
	TXN_CATCH();

	static void ReInitOurVariables()
	{
		for ( const auto& var : GameVariablesToReset )
		{
			std::visit( []( auto&& v ) {
				v.Get() = {};
				}, var );
		}
	}

	static std::string_view TrimWhitespace(std::string_view line)
	{
		const size_t first = line.find_first_not_of(' ');
		if (std::string_view::npos != first)
		{
			const size_t last = line.find_last_not_of(' ');
			line = line.substr(first, (last - first + 1));
		}
		return line;
	}

	static void (*orgLoadPickup)(const char* line);
	static void LoadPickup_SaveLine(const char* line)
	{
		orgLoadPickup(line);
		PickupDefs.emplace_back(TrimWhitespace(line));
	}

	static void (*orgLoadCarGenerator)(const char* line, int originScene);
	static void LoadCarGenerator_SaveLine(const char* line, int originScene)
	{
		orgLoadCarGenerator(line, originScene);
		CarGeneratorDefs.emplace_back(TrimWhitespace(line));
	}

	static void (*orgLoadStuntJump)(const char* line);
	static void LoadStuntJump_SaveLine(const char* line)
	{
		orgLoadStuntJump(line);
		StuntJumpDefs.emplace_back(TrimWhitespace(line));
	}

	static void ReloadObjectDefinitionsAfterReinit()
	{
		for (const auto& pickup : PickupDefs)
		{
			orgLoadPickup(pickup.c_str());
		}
		for (const auto& carGenerator : CarGeneratorDefs)
		{
			orgLoadCarGenerator(carGenerator.c_str(), 0);
		}
		for (const auto& stuntJump : StuntJumpDefs)
		{
			orgLoadStuntJump(stuntJump.c_str());
		}
	}

	template<std::size_t Index>
	static void (*orgReInitGameObjectVariables)();

	template<std::size_t Index>
	void ReInitGameObjectVariables()
	{
		// First reinit "our" variables in case stock ones rely on those during resetting
		ReInitOurVariables();
		orgReInitGameObjectVariables<Index>();

		// Then after the normal restart, re-instate pickups, car generators and stunt jumps from text IPLs as they have been
		ReloadObjectDefinitionsAfterReinit();
	}
	HOOK_EACH_INIT(ReInitGameObjectVariables, orgReInitGameObjectVariables, ReInitGameObjectVariables);
}

namespace LightbeamFix
{
	static bool hookedSuccessfully = false;

	static CVehicle* currentHeadLightBeamVehicle;
	void SetCurrentVehicle( CVehicle* vehicle )
	{
		currentHeadLightBeamVehicle = vehicle;
	}

	template<RwRenderState State>
	class RenderStateWrapper
	{
	private:
		static inline void* SavedState;

		static void PushState( RwRenderState state, void* value )
		{
			assert( State == state );
			RwRenderStateGet( state, &SavedState );
			RwRenderStateSet( state, value );
		}
		static inline const auto pPushState = &PushState;

		static void PopState( RwRenderState state, void* value )
		{
			assert( State == state );

			void* valueToRestore = SavedState;
			if constexpr ( State == rwRENDERSTATECULLMODE )
			{
				assert( currentHeadLightBeamVehicle != nullptr );
				if ( currentHeadLightBeamVehicle != nullptr && currentHeadLightBeamVehicle->IgnoresLightbeamFix() )
				{
					valueToRestore = value;
				}
			}

			RwRenderStateSet( state, valueToRestore );

			// Restore states R* did not restore after changing them
			if constexpr ( State == rwRENDERSTATEDESTBLEND )
			{
				RenderStateWrapper<rwRENDERSTATESHADEMODE>::PopAnotherState();
			}
			if constexpr ( State == rwRENDERSTATECULLMODE )
			{
				RenderStateWrapper<rwRENDERSTATEALPHATESTFUNCTION>::PopAnotherState();
				RenderStateWrapper<rwRENDERSTATEALPHATESTFUNCTIONREF>::PopAnotherState();
			}
		}
		static inline const auto pPopState = &PopState;

	public:
		static void PopAnotherState()
		{
			PopState( State, nullptr );
		}

		static inline const uintptr_t PushStatePPtr = reinterpret_cast<uintptr_t>(&pPushState) - 0x20;
		static inline const uintptr_t PopStatePPtr = reinterpret_cast<uintptr_t>(&pPopState) - 0x20;
	};

}

namespace TrueInvincibility
{
	static bool isEnabled = false;
	static uintptr_t WillKillJumpBack;

	__declspec(naked) static void ComputeWillKillPedHook()
	{
		_asm
		{
			cmp		dword ptr [ebp+0xC], WEAPONTYPE_LAST_WEAPONTYPE
			jl		ComputeWillKillPedHook_DoNotKill
			cmp		isEnabled, 0
			je		ComputeWillKillPedHook_Kill
			cmp		dword ptr [ebp+0xC], WEAPONTYPE_UZI_DRIVEBY
			jne		ComputeWillKillPedHook_Kill

		ComputeWillKillPedHook_DoNotKill:
			pop     esi
			pop     ebp
			pop     ebx
			ret    	0xC

		ComputeWillKillPedHook_Kill:
			jmp		WillKillJumpBack
		}
	}
}

namespace Localization
{
	static int8_t forcedUnits = -1; // 0 - metric, 1 - imperial

	bool IsMetric_LocaleBased()
	{
		if ( forcedUnits != -1 ) return forcedUnits == 0;

		unsigned int LCData;
		if ( GetLocaleInfo( LOCALE_USER_DEFAULT, LOCALE_IMEASURE|LOCALE_RETURN_NUMBER, reinterpret_cast<LPTSTR>(&LCData), sizeof(LCData) / sizeof(TCHAR) ) != 0 )
		{
			return LCData == 0;
		}

		// If fails, default to metric. Hopefully never fails though
		return true;
	}

	// Only used in "new binary" executables
	bool* germanGame;
	bool* frenchGame;
	bool* nastyGame;
	void SetUncensoredGame()
	{
		*germanGame = false;
		*frenchGame = false;
		*nastyGame = true;
	}

	void EmptyStub()
	{
	}
}


// Reintroduced corona rotation
namespace CoronaRotationFix
{
	static void (*orgRenderOneXLUSprite_Rotate_Aspect)(float, float, float, float, float, uint8_t, uint8_t, uint8_t, short, float, float, uint8_t);
	void RenderOneXLUSprite_Rotate_Aspect_SilentPatch( float a1, float a2, float a3, float a4, float a5, uint8_t a6, uint8_t a7, uint8_t a8, short a9, float recipz, float, uint8_t a12 )
	{
		orgRenderOneXLUSprite_Rotate_Aspect( a1, a2, a3, a4, a5, a6, a7, a8, a9, recipz, 20.0f * recipz, a12 );
	}
};

// ============= Static shadow alpha fix =============
namespace StaticShadowAlphaFix
{
	static void (*orgRenderStaticShadows)();
	static void RenderStaticShadows_StateFix()
	{
		RwScopedRenderState<rwRENDERSTATEALPHATESTFUNCTION> state;

		RwRenderStateSet( rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONALWAYS );
		orgRenderStaticShadows();
	}

	static void (*orgRenderStoredShadows)();
	static void RenderStoredShadows_StateFix()
	{
		RwScopedRenderState<rwRENDERSTATEALPHATESTFUNCTION> state;

		RwRenderStateSet( rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONALWAYS );
		orgRenderStoredShadows();
	}
};


// ============= Disable building pipeline for skinned objects (like parachute) =============
namespace SkinBuildingPipelineFix
{
	static RpAtomic* (*orgCustomBuildingDNPipeline_CustomPipeAtomicSetup)(RpAtomic* atomic);
	static RpAtomic* CustomBuildingDNPipeline_CustomPipeAtomicSetup_Skinned(RpAtomic* atomic)
	{
		RxPipeline* pipeline;
		RpAtomicGetPipeline(atomic, &pipeline);
		if (pipeline != nullptr && pipeline->pluginId == rwID_SKINPLUGIN)
		{
			return atomic;
		}

		return orgCustomBuildingDNPipeline_CustomPipeAtomicSetup(atomic);
	}
};

// ============= Moonphases fix =============
namespace MoonphasesFix
{
	static bool HasGameBindings()
	{
		return CPNGFile::HasGameBindings() && RWBindings::RwD3D9SetRenderState();
	}

	// TODO: Reintroduce moon phases to Steam/RGL version
	// Call to RenderOneXLUSprite provides all required data except the moon mask and CClock::ms_nGameClockDays
	static void (*orgRenderOneXLUSprite)(float, float, float, float, float, uint8_t, uint8_t, uint8_t, int16_t, float, uint8_t, uint8_t, uint8_t);

	// By aap
	static void RenderOneXLUSprite_MoonPhases( float arg1, float arg2, float arg3, float arg4, float arg5, uint8_t red, uint8_t green, uint8_t blue, int16_t mult, float arg10, uint8_t alpha, uint8_t arg12, uint8_t arg13 )
	{
		static RwTexture*	gpMoonMask = [] () {

			// load from file
			RwTexture* mask = CPNGFile::ReadFromFile("lunar.png");
			if (mask == nullptr)
			{
				const HMODULE module = reinterpret_cast<HMODULE>(&__ImageBase);

				// Load from memory
				HRSRC resource = FindResource(module, MAKEINTRESOURCE(IDB_LUNAR64), RT_RCDATA);
				if (resource != nullptr)
				{
					HGLOBAL loadedResource = LoadResource(module, resource);
					if (loadedResource != nullptr)
					{
						mask = CPNGFile::ReadFromMemory(LockResource(loadedResource), SizeofResource(module, resource));
					}
				}
			}
			return mask;
		} ();

		RwScopedRenderState<rwRENDERSTATEALPHATESTFUNCTION> alphaTest;

		if ( gpMoonMask != nullptr )
		{
			RwRenderStateSet( rwRENDERSTATETEXTURERASTER, RwTextureGetRaster(gpMoonMask) );
		}

		RwRenderStateSet( rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONALWAYS );
		RwRenderStateSet( rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA );
		RwRenderStateSet( rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO );
		RwD3D9SetRenderState( D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALPHA );

		orgRenderOneXLUSprite( arg1, arg2, arg3, arg4, arg5, red, green, blue, mult, arg10, alpha, arg12, arg13 );

		RwD3D9SetRenderState( D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALPHA | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_RED );
	}
}

// ============= Disallow moving cam up/down with mouse when looking back/left/right in vehicle =============
namespace FollowCarMouseCamFix
{
	static ExternalRef camLookDirection(AddressByVersion<uint32_t**>(0x525526 + 2));

	static bool HasGameBindings()
	{
		return EnsureBindings(camLookDirection);
	}

	static void* (*orgGetPad)(int);
	static bool* orgUseMouse3rdPerson;

	static bool useMouseAndLooksForwards;
	static void* getPadAndSetFlag( int padNum )
	{
		useMouseAndLooksForwards = *orgUseMouse3rdPerson && camLookDirection.Get() == 3;
		return orgGetPad( padNum );
	}
};

// ======= Tie handlebar movement to the steering animations on Quadbike, fixes odd animation interpolations at low speeds =======
namespace QuadbikeHandlebarAnims
{
	static const float POW_CONSTANT = 0.86f;
	static const float SLOW_SPEED_THRESHOLD = 0.02f;
	__declspec(naked) static void ProcessRiderAnims_FixInterp()
	{
		_asm
		{
			xor		edx, edx
			cmp     [esp+0x130-0x100], edx // Reverse animation
			jne		FuncSetToZero
			cmp     [esp+0x130-0xF8], edx // Drive-by animation
			jne		FuncSetToZero
			fld     dword ptr [esp+0x130-0x108]
			fabs
			fcomp	SLOW_SPEED_THRESHOLD
			fnstsw  ax
			test    ah, 5
			jp		FuncReturn

		FuncSetToZero:
			mov		[esp+0x130-0x118], edx

		FuncReturn:
			fld		POW_CONSTANT
			ret
		}
	}

	static uint32_t savedClumpAssociation;
}


// ======= Modify the radio station change animation to only affect the right hand, and disable it on the Kart =======
namespace RadioStationChangeAnimBlending
{
	static bool HasGameBindings()
	{
		return EnsureBindings(RpAnimBlendClumpGetAssociation);
	}

	// Disable all bones but the right hand and arm, and head/neck, so the animation looks better on different vehicles
	static CAnimBlendAssociation* DisableBones(CAnimBlendAssociation* animAssociation, bool bDisablePartial)
	{
		if (animAssociation != nullptr)
		{
			for (CAnimBlendNode& node : animAssociation->GetNodes())
			{
				CAnimBlendSequence* sequence = node.GetSequence();
				if (sequence != nullptr)
				{
					if (sequence->HasBoneTag())
					{
						switch (sequence->GetBoneTag())
						{
						// Keep only those bones, disable the rest
						case BONETAG_HEAD:
						case BONETAG_R_CLAVICLE:
						case BONETAG_R_UPPERARM:
						case BONETAG_R_FOREARM:
						case BONETAG_R_HAND:
						case BONETAG_R_FINGERS:
						case BONETAG_R_FINGER01:
						case BONETAG_R_BREAST:
							break;

						default:
							node.SetSequence(nullptr);
							break;
						}
					}
				}
			}
			if (bDisablePartial)
			{
				animAssociation->ClearFlag(ABA_FLAG_ISPARTIAL);
			}
		}

		return animAssociation;
	}

	static CAnimBlendAssociation* (*orgAnimManagerBlendAnimation)(RpClump*, uint32_t, uint32_t, float);
	CAnimBlendAssociation* AnimManagerBlendAnimation_DisableBones(RpClump* clump, uint32_t assocGroupId, uint32_t animationId, float rate)
	{
		if (RpAnimBlendClumpGetAssociation.Call(clump, 95) != nullptr) // ANIM_STD_CAR_SIT_KART
		{
			return nullptr;
		}

		// For the standing animation, disable the partial/additive flag so CJ doesn't reach as far for the knob
		const bool bIsBoatDrive = RpAnimBlendClumpGetAssociation.Call(clump, 81) != nullptr; // ANIM_STD_BOAT_DRIVE
		return DisableBones(orgAnimManagerBlendAnimation(clump, assocGroupId, animationId, rate), bIsBoatDrive);
	}
};


// ============= Fix a memory leak when taking photos =============
namespace CameraMemoryLeakFix
{
	__declspec(naked) static void psGrabScreen_UnlockAndReleaseSurface()
	{
		_asm
		{
			// Preserve the function result so we don't need two ASM hooks
			push	eax

			mov		eax, [esp+0x34-0x2C]
			mov		edx, [eax]
			push	eax
			call	dword ptr [edx+0x38] // IDirect3DSurface9.UnlockRect

			mov		eax, [esp+0x34-0x2C]
			mov		edx, [eax]
			push	eax
			call	dword ptr [edx+0x8] // IDirect3DSurface9.Release

			pop		eax
			pop		ebp
			add		esp, 0x2C
			retn
		}
	}

}


// ============= Fix crosshair issues when sniper rifle is equipped and a photo is taken by a gang member =============
namespace CameraCrosshairFix
{
	static bool HasGameBindings()
	{
		return EnsureBindings(bDrawCrossHair);
	}

	CWeaponInfo* (*orgGetWeaponInfo)(eWeaponType, signed char);
	CWeaponInfo* GetWeaponInfo_OrCamera(eWeaponType weaponType, signed char type)
	{
		return orgGetWeaponInfo(bDrawCrossHair.Get() != 2 ? weaponType : WEAPONTYPE_CAMERA, type);
	}
}


// ============= Cancel the Drive By task of biker cops when losing the wanted level =============
namespace BikerCopsDriveByFix
{
	static void (*orgJoinCarWithRoadSystem)(CVehicle* vehicle);
	void JoinCarWithRoadSystem_AbortDriveByTask(CVehicle* vehicle)
	{
		orgJoinCarWithRoadSystem(vehicle);

		CPed* driver = vehicle->GetDriver();
		if (driver != nullptr)
		{
			CPedIntelligence* driverIntelligence = driver->GetPedIntelligence();
			if (driverIntelligence != nullptr)
			{
				// If the driver has a sequence, it's the fourth one
				CTask* primaryTask = driverIntelligence->GetTaskPrimary();
				if (primaryTask != nullptr && primaryTask->GetTaskType() == 244) // TASK_COMPLEX_SEQUENCE
				{
					// If the sequence contains a TASK_SIMPLE_GANG_DRIVEBY, abort it
					CTaskComplexSequence* taskSequence = static_cast<CTaskComplexSequence*>(primaryTask);
					if (taskSequence->Contains(1022))
					{
						taskSequence->MakeAbortable(driver, 1, nullptr);
					}
				}
			}
		}
	}
}


// ============= Fix miscolored racing checkpoints if no other marker was drawn before them =============
namespace RacingCheckpointsRender
{
	static RpClump* (*orgRpClumpRender)(RpClump* clump);
	static RpClump* RpClumpRender_SetLitFlag(RpClump* clump)
	{
		RpClumpForAllAtomics(clump, [](RpAtomic* atomic)
			{
				RpGeometry* geometry = RpAtomicGetGeometry(atomic);
				RpGeometrySetFlags(geometry, RpGeometryGetFlags(geometry) | rpGEOMETRYMODULATEMATERIALCOLOR);
				return atomic;
			});
		return orgRpClumpRender(clump);
	}
}

// ============= Correct an improperly decrypted CPlayerPedData::operator= that broke gang recruiting after activating replays =============
namespace PlayerPedDataAssignment
{
	__declspec(naked) static void AssignmentOp_Hoodlum()
	{
		_asm
		{
			xor     edx, [ecx+0x34]
			and     edx, 1
			xor     [eax+0x34], edx
			mov     esi, [eax+0x34]
			mov     edx, [ecx+0x34]
			xor     edx, esi
			and     edx, 2
			xor     edx, esi
			mov     [eax+0x34], edx
			mov     esi, [ecx+0x34]
			xor     esi, edx
			and     esi, 4
			xor     esi, edx
			mov     [eax+0x34], esi
			mov     edx, [ecx+0x34]
			xor     edx, esi
			and     edx, 8
			xor     edx, esi
			mov     [eax+0x34], edx
			mov     esi, [ecx+0x34]
			xor     esi, edx
			and     esi, 0x10
			xor     esi, edx
			mov     [eax+0x34], esi
			mov     edx, [ecx+0x34]
			xor     edx, esi
			and     edx, 0x20
			xor     edx, esi
			mov     [eax+0x34], edx
			mov     esi, [ecx+0x34]
			xor     esi, edx
			and     esi, 0x40
			xor     esi, edx
			mov     [eax+0x34], esi
			mov     edx, [ecx+0x34]
			xor     edx, esi
			and     edx, 0x80
			xor     edx, esi
			mov     [eax+0x34], edx
			mov     esi, [ecx+0x34]
			xor     esi, edx
			and     esi, 0x100
			xor     esi, edx
			mov     [eax+0x34], esi
			mov     edx, [ecx+0x34]
			ret
		}
	}

	__declspec(naked) static void AssignmentOp_Compact()
	{
		_asm
		{
			call	AssignmentOp_Hoodlum
			xor     edx, esi
			and     edx, 0x200
			ret
		}
	}
}


// ============= Spawn lapdm1 (biker cop) correctly if the script requests one with PEDTYPE_COP =============
namespace GetCorrectPedModel_Lapdm1
{
	__declspec(naked) static void BikerCop_Retail()
	{
		_asm
		{
			cmp		dword ptr [esp+4], PEDTYPE_COP
			jnz		BikerCop_Return
			mov		dword ptr [eax], 1

		BikerCop_Return:
			ret		8
		}
	}

}


// ============= Only allow impounding cars and bikes (and their subclasses), as impounding helicopters, planes, boats makes no sense =============
namespace RestrictImpoundVehicleTypes
{
	template<std::size_t Index>
	static bool (*orgIsThisVehicleInteresting)(CVehicle* vehicle);

	template<std::size_t Index>
	static bool IsThisVehicleInteresting_AndCanBeImpounded(CVehicle* vehicle)
	{
		return vehicle->CanThisVehicleBeImpounded() && orgIsThisVehicleInteresting<Index>(vehicle);
	}

	HOOK_EACH_INIT(ShouldImpound, orgIsThisVehicleInteresting, IsThisVehicleInteresting_AndCanBeImpounded)
}


// ============= Fix PlayerPed replay crashes =============
// 1. Crash when starting a mocap cutscene after playing a replay wearing different clothes to the ones CJ has currently
// 2. Crash when playing back a replay with a different motion group animation (fat/muscular/normal) than the current one
namespace ReplayPlayerPedCrashFixes
{
	// FLA compatibility
	static const ExternalValue<int32_t> s_animGroupIDOffset(AddressByVersion<int32_t*>(0x5A814C + 2));

	static bool HasGameBindings()
	{
		return EnsureBindings(s_animGroupIDOffset, CClothes::RebuildPlayer, GetAnimationBlockIndex, RequestModel, LoadAllRequestedModels) && HasGameBindings_FindPlayer();
	}

	static void (*orgRestoreStuffFromMem)();
	static void RestoreStuffFromMem_RebuildPlayer()
	{
		orgRestoreStuffFromMem();
		CClothes::RebuildPlayer.Call(FindPlayerPed(), false);
	}

	static void LoadAllMotionGroupAnims()
	{
		const uint32_t animGroupIDOffset = s_animGroupIDOffset.Get();
		RequestModel.Call(GetAnimationBlockIndex.Call("fat") + animGroupIDOffset, 18);
		RequestModel.Call(GetAnimationBlockIndex.Call("muscular") + animGroupIDOffset, 18);

		LoadAllRequestedModels.Call(true);
	}

	static void (*orgRebuildPlayer)(CPlayerPed*, bool);
	static void RebuildPlayer_LoadAllMotionGroupAnims(CPlayerPed* ped, bool bForReplay)
	{
		orgRebuildPlayer(ped, bForReplay);
		LoadAllMotionGroupAnims();
	}
}


// ============= Fix planes spawning in places where they crash easily =============
// CPlane::FindPlaneCreationCoors passes the XY position to CCollision::CheckCameraCollisionBuildings,
// while the function accepts the sector numbers
namespace FindPlaneCreationCoorsFix
{
	static std::pair<int, int> GetSectorNumbersFromPosition(int posX, int posY)
	{
		// FLA compatibility
		// This function is used only for 1.0 as new binaries use a divisor and not a multiplier,
		// so it's fine to reference addresses directly!
		const float& SectorMultX = **(float**)(0x41ACED + 2);
		const float& SectorAddX = **(float**)(0x41ACF6 + 2);
		const float& SectorMultY = **(float**)(0x41AD3C + 2);
		const float& SectorAddY = **(float**)(0x41AD42 + 2);

		const int SectorX = static_cast<int>(posX * SectorMultX + SectorAddX);
		const int SectorY = static_cast<int>(posY * SectorMultY + SectorAddY);
		return {SectorX, SectorY};
	}

	static bool (*orgCheckCameraCollisionBuildings)(int sectorX, int sectorY, void*, void*, void*, void*);
	static bool CheckCameraCollisionBuildings_FixParams(int posX, int posY, void* a3, void* a4, void* a5, void* a6)
	{
		auto [sectorX, sectorY] = GetSectorNumbersFromPosition(posX, posY);
		return orgCheckCameraCollisionBuildings(sectorX, sectorY, a3, a4, a5, a6);
	}

}


// ============= Allow hovering on the Jetpack with Keyboard + Mouse controls =============
// Does not modify any other controls, only hovering
namespace JetpackKeyboardControlsHover
{
	static void (__thiscall *orgGetLookBehindForCar)(void*);

	static void* ProcessControlInput_DontHover;
	static void* ProcessControlInput_Hover;

	__declspec(naked) static void ProcessControlInput_HoverWithKeyboard()
	{
		_asm
		{
			mov		ecx, ebp
			call	orgGetLookBehindForCar
			test	al, al
			jnz		Hovering
			mov		ecx, ebp
			mov		byte ptr [esi+0xD], 0
			jmp		ProcessControlInput_DontHover

		Hovering:
			jmp		ProcessControlInput_Hover
		}
	}

}


// ============= During riots, don't target the player group during missions =============
// Fixes recruited homies panicking during Los Desperados and other riot-time missions
namespace RiotDontTargetPlayerGroupDuringMissions
{
	static bool HasGameBindings()
	{
		return EnsureBindings(IsPlayerOnAMission);
	}

	static void* SkipTargetting;
	static void* DontSkipTargetting;

	__declspec(naked) static void CheckIfInPlayerGroupAndOnAMission()
	{
		_asm
		{
			cmp     byte ptr [ebp+0x2D0], 1
			jne		NotInGroup
			call	IsPlayerOnAMission
			test	al, al
			jz		NotOnAMission
			jmp		SkipTargetting

		NotOnAMission:
			cmp     byte ptr [ebp+0x2D0], 1

		NotInGroup:
			jmp		DontSkipTargetting
		}
	}

}


// ============= Fixed vehicles exploding twice if the driver leaves the car while it's exploding =============
namespace RemoveDriverStatusFix
{
	__declspec(naked) static void RemoveDriver_SetStatus()
	{
		// if (m_nStatus != STATUS_WRECKED)
		//   m_nStatus = STATUS_ABANDONED;
		_asm
		{
			mov		bl, [edi+0x36]
			mov		al, bl
			and		bl, 0xF8
			cmp		bl, 0x28
			je		DontSetStatus
			and     al, 7
			or      al, 0x20

		DontSetStatus:
			ret
		}
	}

	static void (__thiscall *orgPrepareVehicleForPedExit)(CTaskComplexCarSlowBeDraggedOut* task, CPed* ped);
	static void __fastcall PrepareVehicleForPedExit_WreckedCheck(CTaskComplexCarSlowBeDraggedOut* task, void*, CPed* ped)
	{
		if (task->m_pVehicle->GetStatus() != STATUS_WRECKED)
		{
			orgPrepareVehicleForPedExit(task, ped);
		}
	}
}


// ============= Fixed shooting stars rendering black =============
namespace ShootingStarsFix
{
	static void* (*orgRwIm3DTransform)(RwIm3DVertex* pVerts, RwUInt32 numVerts, RwMatrix* ltm, RwUInt32 flags);
	static void* RwIm3DTransform_UnsetTexture(RwIm3DVertex* pVerts, RwUInt32 numVerts, RwMatrix* ltm, RwUInt32 flags)
	{
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, nullptr);
		return orgRwIm3DTransform(pVerts, numVerts, ltm, flags);
	}
}


// ============= Enable directional lights on flying car components =============
namespace LitFlyingComponents
{
	static void (*orgWorldAdd)(CEntity*);
	static void WorldAdd_SetLightObjectFlag(CEntity* entity)
	{
		entity->bLightObject = true;
		orgWorldAdd(entity);
	}
}


// ============= Make script randomness 16-bit, like on PS2 =============
namespace Rand16bit
{
	template<std::size_t Index>
	static int (*orgRand)();

	template<std::size_t Index>
	static int rand16bit()
	{
		const int bottomBits = orgRand<Index>();
		const int topBit = (orgRand<Index>() & 1) << 15;
		return bottomBits | topBit;
	}

	HOOK_EACH_INIT(Rand, orgRand, rand16bit);
}


// ============= Improved resolution selection dialog =============
namespace NewResolutionSelectionDialog
{
	static IDirect3D9** ppRWD3D9;
	static void* FrontEndMenuManager;
	static char* (*orgGetDocumentsPath)();

	static constexpr const char* SettingsFileName = "device_remembered.set";

	static bool HasGameBindings()
	{
		return RWBindings::RwEngineSetSubSystem();
	}

	static bool ShouldSkipDeviceSelection()
	{
		char cTmpPath[MAX_PATH];
		PathCombineA(cTmpPath, orgGetDocumentsPath(), SettingsFileName);

		bool bSkip = false;

		FILE* hFile = nullptr;
		if (fopen_s(&hFile, cTmpPath, "r") == 0)
		{
			unsigned int val = 0;
			bSkip = fscanf_s(hFile, "%u", &val) == 1 && val != 0;
			fclose(hFile);
		}
		return bSkip;
	}

	static void RememberDeviceSelection(bool bDoNotShowAgain)
	{
		char cTmpPath[MAX_PATH];
		PathCombineA(cTmpPath, orgGetDocumentsPath(), SettingsFileName);

		FILE* hFile = nullptr;
		if (fopen_s(&hFile, cTmpPath, "w") == 0)
		{
			fprintf_s(hFile, "%u", bDoNotShowAgain ? 1 : 0);
			fclose(hFile);
		}
	}

	static RwSubSystemInfo* (*orgRwEngineGetSubSystemInfo)(RwSubSystemInfo *subSystemInfo, RwInt32 subSystemIndex);
	static RwSubSystemInfo *RwEngineGetSubSystemInfo_GetFriendlyNames(RwSubSystemInfo *subSystemInfo, RwInt32 subSystemIndex)
	{
		// If we can't do any our work, fall back to the original game functions that may already by customized by other mods
		if (*ppRWD3D9 == nullptr)
		{
			return orgRwEngineGetSubSystemInfo(subSystemInfo, subSystemIndex);
		}

		D3DADAPTER_IDENTIFIER9 identifier;
		if (FAILED((*ppRWD3D9)->GetAdapterIdentifier(subSystemIndex, 0, &identifier)))
		{
			return orgRwEngineGetSubSystemInfo(subSystemInfo, subSystemIndex);
		}

		static const auto friendlyNames = FriendlyMonitorNames::GetNamesForDevicePaths();

		// If we can't find the friendly name, either because it doesn't exist or we're on an ancient Windows, fall back to the device name
		auto it = friendlyNames.find(identifier.DeviceName);
		if (it != friendlyNames.end())
		{
			strncpy_s(subSystemInfo->name, it->second.c_str(), _TRUNCATE);
		}
		else
		{
			strncpy_s(subSystemInfo->name, identifier.Description, _TRUNCATE);
		}

		return subSystemInfo;
	}

	static size_t MenuManagerAdapterOffset = 0xDC;
	static RwInt32 (*orgRwEngineGetCurrentSubSystem)();
	static RwInt32 RwEngineGetCurrentSubSystem_FromSettings()
	{
		// If we can't do any our work, fall back to the original game functions that may already by customized by other mods
		if (*ppRWD3D9 == nullptr)
		{
			return orgRwEngineGetCurrentSubSystem();
		}

		RwInt32 subSystem = *reinterpret_cast<RwInt32*>(static_cast<char*>(FrontEndMenuManager) + MenuManagerAdapterOffset);
		if (subSystem > 0)
		{
			// Force the device selection dialog to show again if anything is wrong
			bool bResetDisplay = false;
			if ((*ppRWD3D9)->GetAdapterCount() <= (UINT)subSystem)
			{
				subSystem = 0;
				bResetDisplay = true;
			}
			if (RwEngineSetSubSystem(subSystem) == FALSE || bResetDisplay)
			{
				RememberDeviceSelection(false);
				return 0;
			}
		}
		return subSystem;
	}

	static void CreateNewButtonTooltip(HINSTANCE hInstance, HWND hDlg)
	{
		HWND hCheckbox = GetDlgItem(hDlg, IDC_REMEMBERRESCHOICE);
		HWND hwndTip = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL, WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
						hDlg, nullptr, hInstance, nullptr);

		if (hCheckbox == nullptr || hwndTip == nullptr)
		{
			return;
		}

		TOOLINFO toolInfo { sizeof(toolInfo) };
		toolInfo.hwnd = hDlg;
		toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
		toolInfo.uId = (UINT_PTR)hCheckbox;
		toolInfo.lpszText = (LPWSTR)TEXT("Delete 'device_remembered.set' from GTA San Andreas User Files to show this dialog again.");

		SendMessage(hwndTip, TTM_ADDTOOL, 0, (LPARAM)&toolInfo);
	}

	struct WrappedDialocFunc
	{
		DLGPROC lpDialogFunc;
		LPARAM dwInitParam;
	};
	static INT_PTR CALLBACK CustomDlgProc(HWND window, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		if (msg == WM_INITDIALOG)
		{
			const WrappedDialocFunc* data = reinterpret_cast<WrappedDialocFunc*>(lParam);
			SetWindowLongPtr(window, DWLP_USER, reinterpret_cast<LONG_PTR>(data->lpDialogFunc));

			data->lpDialogFunc(window, msg, wParam, data->dwInitParam);

			// The stock dialog func loaded the selected adapter and resolution at this point,
			// we can bail if we don't need to show the dialog
			if (ShouldSkipDeviceSelection())
			{
				// The game inits the selected resolution weirdly, and corrects it in the IDOK handler
				// so let's invoke it manually (bleh)
				data->lpDialogFunc(window, WM_COMMAND, IDOK, 0);
				return FALSE;
			}

			HMODULE hGameModule = GetModuleHandle(nullptr);
			SendMessage(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(LoadIcon(hGameModule, MAKEINTRESOURCE(100))));
			CreateNewButtonTooltip(hGameModule, window);

			// Return TRUE instead of FALSE on init, as we removed a manual SetFocus from the init function
			// and we want to rely on Windows to give us focus.
			return TRUE;
		}

		// Custom handling for IDCANCEL (IDOK is fine)
		if (msg == WM_COMMAND)
		{
			if (LOWORD(wParam) == IDCANCEL)
			{
				EndDialog(window, 0);
				return TRUE;
			}

			// Just remember the selection, let the game handle the rest
			if (LOWORD(wParam) == IDOK && IsDlgButtonChecked(window, IDC_REMEMBERRESCHOICE) == BST_CHECKED)
			{
				RememberDeviceSelection(true);
			}
		}

		DLGPROC origProc = reinterpret_cast<DLGPROC>(GetWindowLongPtr(window, DWLP_USER));
		if (origProc != nullptr)
		{
			return origProc(window, msg, wParam, lParam);
		}
		return FALSE;
	}

	static INT_PTR WINAPI DialogBoxParamA_New(HINSTANCE /*hInstance*/, LPCSTR /*lpTemplateName*/, HWND /*hWndParent*/, DLGPROC lpDialogFunc, LPARAM dwInitParam)
	{
		int32_t (WINAPI *pSetThreadDpiAwarenessContext)(int32_t dpiContext) = nullptr;
		int32_t oldDpiContext = 0;

		// Specify the dialog as DPI unaware, so Windows scales it by itself
		HMODULE user32Module = LoadLibraryW(L"user32");
		if (user32Module != nullptr)
		{
			pSetThreadDpiAwarenessContext = (decltype(pSetThreadDpiAwarenessContext))GetProcAddress(user32Module, "SetThreadDpiAwarenessContext");
		}

		if (pSetThreadDpiAwarenessContext != nullptr)
		{
			oldDpiContext = pSetThreadDpiAwarenessContext(/*DPI_AWARENESS_CONTEXT_UNAWARE*/-1);
		}

		ACTCTX actCtx { sizeof(actCtx) };
		actCtx.hModule = reinterpret_cast<HMODULE>(&__ImageBase);
		actCtx.lpResourceName = MAKEINTRESOURCE(2);
		actCtx.dwFlags = ACTCTX_FLAG_HMODULE_VALID | ACTCTX_FLAG_RESOURCE_NAME_VALID;

		ULONG_PTR cookie = 0;
		bool bContextActivated = false;

		HANDLE hActCtx = CreateActCtx(&actCtx);
		if (hActCtx != INVALID_HANDLE_VALUE)
		{
			bContextActivated = ActivateActCtx(hActCtx, &cookie) != FALSE;
		}

		// Include our own context to allow for custom message handling
		const WrappedDialocFunc origDlgProc { lpDialogFunc, dwInitParam };
		const INT_PTR result = DialogBoxParam(reinterpret_cast<HMODULE>(&__ImageBase), MAKEINTRESOURCE(IDD_RESSELECT), nullptr, CustomDlgProc, reinterpret_cast<LPARAM>(&origDlgProc));

		if (bContextActivated)
		{
			DeactivateActCtx(0, cookie);
		}
		if (hActCtx != INVALID_HANDLE_VALUE)
		{
			ReleaseActCtx(hActCtx);
		}

		if (pSetThreadDpiAwarenessContext != nullptr)
		{
			pSetThreadDpiAwarenessContext(oldDpiContext);
		}
		if (user32Module != nullptr)
		{
			FreeLibrary(user32Module);
		}
		return result;
	}
	static auto* const pDialogBoxParamA_New = &DialogBoxParamA_New;

	static HWND WINAPI SetFocus_NOP(HWND)
	{
		return nullptr;
	}
	static auto* const pSetFocus_NOP = &SetFocus_NOP;
}


// ============= Fix credits not scaling to resolution =============
// Also makes the shadow scale properly, as they haven't done that either...
// But since this seems to be the only place, don't pull in the fix from Vice City,
// fix it here instead
namespace CreditsScalingFixes
{
	static bool HasGameBindings()
	{
		return UIScales::HasGameBindings();
	}

	static const unsigned int FIXED_RES_HEIGHT_SCALE = 448;

	template<std::size_t Index>
	static void (*orgPrintString)(float,float,const wchar_t*);

	template<std::size_t Index>
	static void PrintString_ScaleY(float fX, float fY, const wchar_t* pText)
	{
		using namespace UIScales;
		if constexpr (Index == 1)
		{
			// Fix the shadow X scale - the Y scale will be fixed below
			fX = fX + 1.0f - Stuff2d::Width();
		}
		orgPrintString<Index>(fX, fY * Stuff2d::Height(), pText);
	}

	static void (*orgSetScale)(float X, float Y);
	static void SetScale_ScaleToRes(float X, float Y)
	{
		using namespace UIScales;

		orgSetScale(X * Stuff2d::Width(), Y * Stuff2d::Height());
	}

	HOOK_EACH_INIT(PrintString, orgPrintString, PrintString_ScaleY);
}


// ============= Fix some big messages staying on screen longer at high resolutions due to a cut sliding text feature =============
namespace SlidingTextsScalingFixes
{
	static bool HasGameBindings()
	{
		return UIScales::HasGameBindings();
	}

	static const unsigned int FIXED_RES_WIDTH_SCALE = 640;

	static std::array<float, 6>* pBigMessageX;
	static float* pOddJob2XOffset;

	template<std::size_t BigMessageIndex>
	struct BigMessageSlider
	{
		static inline bool bSlidingEnabled = false;

		template<std::size_t Index>
		static void (*orgPrintString)(float,float,const wchar_t*);

		template<std::size_t Index>
		static void PrintString_Slide(float fX, float fY, const wchar_t* pText)
		{
			// We divide by a constant 640.0, because the X position is meant to slide across the entire screen
			orgPrintString<Index>(bSlidingEnabled ? (*pBigMessageX)[BigMessageIndex] * RsGlobal.Get().MaximumWidth / 640.0f : fX, fY, pText);
		}

		template<std::size_t Index>
		static void (*orgSetRightJustifyWrap)(float wrap);

		template<std::size_t Index>
		static void SetRightJustifyWrap_Slide(float wrap)
		{
			orgSetRightJustifyWrap<Index>(bSlidingEnabled ? -500.0f * UIScales::HudMessages::Width() : wrap);
		}

		HOOK_EACH_INIT(PrintString, orgPrintString, PrintString_Slide);
		HOOK_EACH_INIT(RightJustifyWrap, orgSetRightJustifyWrap, SetRightJustifyWrap_Slide);
	};

	struct OddJob2Slider
	{
		static inline bool bSlidingEnabled = false;

		template<std::size_t Index>
		static void (*orgPrintString)(float,float,const wchar_t*);

		template<std::size_t Index>
		static void PrintString_Slide(float fX, float fY, const wchar_t* pText)
		{
			// We divide by a constant 640.0, because the X position is meant to slide across the entire screen
			if (bSlidingEnabled)
			{
				fX -= *pOddJob2XOffset * RsGlobal.Get().MaximumWidth / 640.0f;
			}
			orgPrintString<Index>(fX, fY, pText);
		}

		HOOK_EACH_INIT(PrintString, orgPrintString, PrintString_Slide);
	};

	template<std::size_t BigMessageIndex>
	template<std::size_t Index>
	void (*BigMessageSlider<BigMessageIndex>::orgPrintString)(float, float, const wchar_t*) = nullptr;

	template<std::size_t BigMessageIndex>
	template<std::size_t Index>
	void (*BigMessageSlider<BigMessageIndex>::orgSetRightJustifyWrap)(float) = nullptr;

	template<std::size_t Index>
	void (*OddJob2Slider::orgPrintString)(float, float, const wchar_t*) = nullptr;
}


// ============= Fix post effects not scaling correctly =============
// Heat haze not rescaling after changing resolution
// Water ripple effect having too high wave frequency at higher resolutions
namespace PostEffectsScalingFixes
{
	static int32_t* pHeatHazeFXTypeLast;

	// Instead of NOPing the call to SetupBackBufferVertex, redirect it to an empty function,
	// so other mods can chain with it fine
	static void (*orgSetupBackBufferVertex)();
	static void SetupBackBufferVertex_Nop()
	{
	}

	template<std::size_t Index>
	static void (*orgSetCurrentVideoMode)(int modeIndex);

	template<std::size_t Index>
	static void SetCurrentVideoMode_SetupPostFX(int modeIndex)
	{
		*pHeatHazeFXTypeLast = -1; // Force heat haze to reinit

		orgSetCurrentVideoMode<Index>(modeIndex);
		orgSetupBackBufferVertex();
	}

	HOOK_EACH_INIT(SetCurrentVideoMode, orgSetCurrentVideoMode, SetCurrentVideoMode_SetupPostFX);

	static bool HasGameBindings_UnderWaterRipple()
	{
		return UIScales::HasGameBindings();
	}

	static void (*orgUnderWaterRipple)(RwRGBA, float, float, int, float, float);
	static void UnderWaterRipple_ScaleFrequency(RwRGBA a1, float xOffset, float yOffset, int a4, float a5, float frequency)
	{
		// Scale frequency counter-proportionally to the resolution height
		// as the function already scales the sine wave frequency to that internally.
		const float freqDivFactor = RsGlobal.Get().MaximumHeight / 480.0f;
		orgUnderWaterRipple(a1, xOffset, yOffset, a4, a5, frequency / freqDivFactor);
	}
}


// ============= Fix heat seeking and gamepad crosshairs not scaling to resolution =============
namespace CrosshairScalingFixes
{
	static bool HasGameBindings()
	{
		return UIScales::HasGameBindings();
	}

	template<std::size_t Index>
	static void (*orgRenderOneXLUSprite_Rotate_Aspect)(float, float, float, float, float, uint8_t, uint8_t, uint8_t, short, float, float, uint8_t);

	template<std::size_t Index>
	static void RenderOneXLUSprite_Rotate_Aspect_Scale(float a1, float a2, float a3, float width, float height, uint8_t a6, uint8_t a7, uint8_t a8, short a9, float a10, float a11, uint8_t a12)
	{
		using namespace UIScales;
		orgRenderOneXLUSprite_Rotate_Aspect<Index>(a1, a2, a3, width * Stuff2d::Width(), height * Stuff2d::Height(), a6, a7, a8, a9, a10, a11, a12);
	}

	template<std::size_t Index>
	static const float* orgSize_GamepadCrosshair;

	template<std::size_t Index>
	static float Size_Recalculated_GamepadCrosshair;

	template<std::size_t... I>
	static void RecalculateSizes_GamepadCrosshair(std::index_sequence<I...>)
	{
		const float multiplier = UIScales::Stuff2d::Height();
		((Size_Recalculated_GamepadCrosshair<I> = *orgSize_GamepadCrosshair<I> * multiplier), ...);
	}

	template<std::size_t Index>
	static const double* orgSize_GamepadCrosshair_Double;

	template<std::size_t Index>
	static double Size_Recalculated_GamepadCrosshair_Double;

	template<std::size_t... I>
	static void RecalculateSizes_GamepadCrosshair_Double(std::index_sequence<I...>)
	{
		const float multiplier = UIScales::Stuff2d::Height();
		((Size_Recalculated_GamepadCrosshair_Double<I> = *orgSize_GamepadCrosshair_Double<I> * multiplier), ...);
	}

	static bool (*orgCalcScreenCoors)(const RwV3d&, RwV3d*, float*, float*, bool, bool);

	template<std::size_t NumFloats, std::size_t NumDoubles>
	static bool CalcScreenCoors_Recalculate(const RwV3d& a1, RwV3d* a2, float* a3, float* a4, bool a5, bool a6)
	{
		RecalculateSizes_GamepadCrosshair(std::make_index_sequence<NumFloats>{});
		RecalculateSizes_GamepadCrosshair_Double(std::make_index_sequence<NumDoubles>{});
		return orgCalcScreenCoors(a1, a2, a3, a4, a5, a6);
	}

	HOOK_EACH_INIT(RenderOneXLUSprite_Rotate_Aspect, orgRenderOneXLUSprite_Rotate_Aspect, RenderOneXLUSprite_Rotate_Aspect_Scale);
	HOOK_EACH_INIT(GamepadCrosshair, orgSize_GamepadCrosshair, Size_Recalculated_GamepadCrosshair);
	HOOK_EACH_INIT(GamepadCrosshair_Double, orgSize_GamepadCrosshair_Double, Size_Recalculated_GamepadCrosshair_Double);
}


// ============= Fix Map screen boundaries and the cursor not scaling to resolution =============
// Debugged by Wesser
namespace MapScreenScalingFixes
{
	static bool HasGameBindings()
	{
		return UIScales::HasGameBindings();
	}


	template<std::size_t Index>
	static const float* orgCursorXSize;

	template<std::size_t Index>
	static float CursorXSize_Recalculated;

	template<std::size_t... I>
	static void RecalculateXSize(std::index_sequence<I...>)
	{
		const float multiplier = UIScales::MenuManager::Width();
		((CursorXSize_Recalculated<I> = *orgCursorXSize<I> * multiplier), ...);
	}

	template<std::size_t Index>
	static const float* orgCursorYSize;

	template<std::size_t Index>
	static float CursorYSize_Recalculated;

	template<std::size_t... I>
	static void RecalculateYSize(std::index_sequence<I...>)
	{
		const float multiplier = UIScales::MenuManager::Height();
		((CursorYSize_Recalculated<I> = *orgCursorYSize<I> * multiplier), ...);
	}

	static void (*orgLimitToMap_Scale)(float* x, float* y);
	static void LimitToMap_Scale(float* x, float* y)
	{
		// LimitToMap assumes it's given scaled coordinates, but then the caller assumes it returns unscaled coordinates.
		// Need to scale them for the call, then unscale again, to save us from some assembly patching.
		const float XScale = UIScales::MenuManager::Width();
		const float YScale = UIScales::MenuManager::Height();

		*x *= XScale;
		*y *= YScale;
		orgLimitToMap_Scale(x, y);

		*x /= XScale;
		*y /= YScale;
	}

	static void (*orgLimitToMap_RecalculateSizes)(float* x, float* y);
	template<std::size_t NumXSize, std::size_t NumYSize>
	static void LimitToMap_RecalculateSizes(float* x, float* y)
	{
		orgLimitToMap_RecalculateSizes(x, y);
		RecalculateXSize(std::make_index_sequence<NumXSize>{});
		RecalculateYSize(std::make_index_sequence<NumYSize>{});
	}

	HOOK_EACH_INIT(CursorXSize, orgCursorXSize, CursorXSize_Recalculated);
	HOOK_EACH_INIT(CursorYSize, orgCursorYSize, CursorYSize_Recalculated);
}


// ============= Fix text background padding not scaling to resolution =============
// Debugged by Wesser
namespace TextRectPaddingScalingFixes
{
	static bool HasGameBindings()
	{
		return UIScales::HasGameBindings();
	}

	template<std::size_t Index>
	static const float* orgPaddingXSize;

	template<std::size_t Index>
	static float PaddingXSize_Recalculated;

	template<std::size_t... I>
	static void RecalculateXSize(std::index_sequence<I...>)
	{
		const float multiplier = UIScales::Font::Width();
		((PaddingXSize_Recalculated<I> = *orgPaddingXSize<I> * multiplier), ...);
	}

	template<std::size_t Index>
	static const float* orgPaddingYSize;

	template<std::size_t Index>
	static float PaddingYSize_Recalculated;

	template<std::size_t... I>
	static void RecalculateYSize(std::index_sequence<I...>)
	{
		const float multiplier = UIScales::Font::Height();
		((PaddingYSize_Recalculated<I> = *orgPaddingYSize<I> * multiplier), ...);
	}

	template<std::size_t Index>
	static const double* orgPaddingYSize_Double;

	template<std::size_t Index>
	static double PaddingYSize_Double_Recalculated;

	template<std::size_t... I>
	static void RecalculateYSize_Double(std::index_sequence<I...>)
	{
		const float multiplier = UIScales::Font::Height();
		((PaddingYSize_Double_Recalculated<I> = *orgPaddingYSize_Double<I> * multiplier), ...);
	}

	static short (*orgProcessCurrentString)(uint8_t, float, float, void*);

	template<std::size_t NumXSize, std::size_t NumYSize>
	static short ProcessCurrentString_Scale(uint8_t a1, float a2, float a3, void* a4)
	{
		RecalculateXSize(std::make_index_sequence<NumXSize>{});
		RecalculateYSize(std::make_index_sequence<NumYSize>{});
		return orgProcessCurrentString(a1, a2, a3, a4);
	}

	HOOK_EACH_INIT(PaddingXSize, orgPaddingXSize, PaddingXSize_Recalculated);
	HOOK_EACH_INIT(PaddingYSize, orgPaddingYSize, PaddingYSize_Recalculated);
	HOOK_EACH_INIT(PaddingYSize_Double, orgPaddingYSize_Double, PaddingYSize_Double_Recalculated);
}


// ============= Fix nitrous recharging faster when reversing the car =============
// By Wesser
namespace NitrousReverseRechargeFix
{
	__declspec(naked) static void NitrousControl_DontRechargeWhenReversing()
	{
		// x = 1.0f; \ if m_fGasPedal >= 0.0f x -= m_fGasPedal;
		_asm
		{
			fld		dword ptr [esi+0x49C]
			fldz
			fcomp   st(1)
			fnstsw  ax
			test    ah, 0x41
			jnz		BiggerOrEqual
			fstp	st
			ret

		BiggerOrEqual:
			fsubp   st(1), st
			ret
		}
	}

}


// ============= Fix Hydra's jet thrusters not displaying due to an uninitialized variable in RwMatrix =============
// By B1ack_Wh1te
namespace JetThrustersFix
{
	// These are technically CMatrix, but for simplicity we use RwMatrix here
	template<std::size_t Index>
	static RwMatrix* (*orgMatrixMultiply)(RwMatrix* out, const RwMatrix* lhs, const RwMatrix* rhs);

	template<std::size_t Index>
	static RwMatrix* MatrixMultiply_ZeroFlags(RwMatrix* out, const RwMatrix* lhs, const RwMatrix* rhs)
	{
		RwMatrix* result = orgMatrixMultiply<Index>(out, lhs, rhs);

		// Technically, this should be the same as RwMatrixUpdate, but this variable is on the stack
		// and completely uninitialized, so zero it completely for consistent results.
		rwMatrixSetFlags(result, 0);

		return result;
	}

	HOOK_EACH_INIT(MatrixMultiply, orgMatrixMultiply, MatrixMultiply_ZeroFlags);
}


// ============= Fix Skimmer not spawning correctly (and shooting up the sky) on Windows 11 24H2 =============
// Missing vehicles.ide values should have always caused issues, but only in 24H2 fgets/LeaveCriticalSection uses enough stack
// to scramble the stale values in CFileLoader::LoadVehicleObject.
namespace SkimmerVehiclesIdeFix
{
	static int (*orgSscanf)(const char* s, const char* format, ...);
	static int sscanf_Defaults(const char* s, const char* format, int* objID, char* modelName, char* texName, char* type, char* handlingID, char* gameName, char* anims, char* vehClass,
				int* frequency, int* flags, int* comprules, int* wheelModelID, float* wheelSize1, float* wheelSize2, int* wheelUpgradeClass)
	{
		*wheelModelID = -1;
		*wheelSize1 = 0.7f;
		*wheelSize2 = 0.7f;
		*wheelUpgradeClass = -1;

		return orgSscanf(s, format, objID, modelName, texName, type, handlingID, gameName, anims, vehClass, frequency, flags, comprules, wheelModelID, wheelSize1, wheelSize2, wheelUpgradeClass);
	}
}

// ============= Fixed most line wraps not scaling to resolution =============
namespace FixedLineWraps
{
	static bool HasGameBindings()
	{
		return UIScales::HasGameBindings();
	}

	// Can be SetWrapx, SetRightJustifyWrap, or SetCentreSize
	template<typename Scaler>
	struct WrapInternal
	{
		template<std::size_t Index>
		static void (*orgWrapFunction)(float);

		template<std::size_t Index>
		static void WrapFunction_LeftAlign(float fLength)
		{
			orgWrapFunction<Index>(fLength * Scaler::Width());
		}

		template<std::size_t Index>
		static void WrapFunction_RightAlign(float fLength)
		{
			const int origin = RsGlobal.Get().MaximumWidth;

			fLength -= origin;
			fLength *= Scaler::Width();
			fLength += origin;

			orgWrapFunction<Index>(fLength);
		}

		template<std::size_t Index>
		static void WrapFunction_FullWidth(float /*fLength*/)
		{
			orgWrapFunction<Index>(static_cast<float>(RsGlobal.Get().MaximumWidth));
		}
	};

	template<typename Scaler>
	template<std::size_t Index>
	void (*WrapInternal<Scaler>::orgWrapFunction)(float) = nullptr;

	struct MenuManager : public WrapInternal<UIScales::MenuManager>
	{
		HOOK_EACH_INIT_CTR(PrintMap_Left, 0, orgWrapFunction, WrapFunction_LeftAlign);
		HOOK_EACH_INIT_CTR(PrintMap_Right, 1, orgWrapFunction, WrapFunction_RightAlign);
		HOOK_EACH_INIT_CTR(PrintMap_FullWidth, 2, orgWrapFunction, WrapFunction_FullWidth);

		HOOK_EACH_INIT_CTR(DrawStandardMenus_Left, 10, orgWrapFunction, WrapFunction_LeftAlign);
		HOOK_EACH_INIT_CTR(DrawStandardMenus_Right, 11, orgWrapFunction, WrapFunction_RightAlign);
	};

	struct Replay : public WrapInternal<UIScales::Replay>
	{
		HOOK_EACH_INIT_CTR(Display_Right, 0, orgWrapFunction, WrapFunction_RightAlign);
	};
}


// ============= Corona flares not scaling to resolution =============
namespace CoronaFlaresScaling
{
	static bool HasGameBindings()
	{
		return UIScales::HasGameBindings();
	}

	template<std::size_t Index>
	static void (*orgRenderBufferedOneXLUSprite2D)(void* x, void* y, float width, float height, void* rgb, void* intens, void* a);
	
	template<std::size_t Index>
	static void RenderBufferedOneXLUSprite2D_Scale(void* x, void* y, float width, float height, void* rgb, void* intens, void* a)
	{
		orgRenderBufferedOneXLUSprite2D<Index>(x, y, width * UIScales::Stuff2d::Width(), height * UIScales::Stuff2d::Height(), rgb, intens, a);
	}

	HOOK_EACH_INIT(RenderOneSprite, orgRenderBufferedOneXLUSprite2D, RenderBufferedOneXLUSprite2D_Scale);
}


// ============= Sun size hack =============
namespace SunSizeHack
{
	static bool bEnableHack = false;

	static float* fFarClipZ;
	static float fHackedFarClipZ;

	static void (*orgDoSunAndMoon)();
	static void DoSunAndMoon_SunSizeHack()
	{
		fHackedFarClipZ = bEnableHack ? std::min(*fFarClipZ, 600.0f) : *fFarClipZ;
		orgDoSunAndMoon();
	}
}


// ============= Display a fallback string if the resolution string is absent =============
namespace AdvancedDisplaySettingsCrashFix
{
	void (*orgAsciiToGxtChar)(const char* src, char* dest);
	void AsciiToGxtChar_NullCheck(const char* src, char* dest)
	{
		if (src == nullptr)
		{
			src = "-";
		}
		orgAsciiToGxtChar(src, dest);
	}
}


// ============= Fix the missing directional multiplier and a broken RAINY_COUNTRYSIDE in PC timecyc.dat =============
namespace TimecycDatMissingDataFix
{
	static int (*orgSscanf)(const char* s, const char* format, ...);
	static int sscanf_TimecycLine(const char* s, const char* format, int* Amb_R, int* Amb_G, int* Amb_B, int* Amb_Obj_R, int* Amb_Obj_G, int* Amb_Obj_B, int* Dir_R, int* Dir_G, int* Dir_B,
			int* SkyTop_R, int* SkyTop_G, int* SkyTop_B, int* SkyBot_R, int* SkyBot_G, int* SkyBot_B, int* SunCore_R, int* SunCore_G, int* SunCore_B, int* SunCorona_R, int* SunCorona_G, int* SunCorona_B,
			float* SunSz, float* SprSz, float* SprBght, int* Shdw, int* LightShd, int* PoleShd, float* FarClp, float* FogSt, float* LightOnGround, int* LowCloudsR, int* LowCloudsG, int* LowCloudsB,
			int* BottomCloudR, int* BottomCloudG, int* BottomCloudB, float* WaterR, float* WaterG, float* WaterB, float* WaterA, float* PostFX1_A, float* PostFX1_R, float* PostFX1_G, float* PostFX1_B,
			float* PostFX2_A, float* PostFX2_R, float* PostFX2_G, float* PostFX2_B, float* Cloud_A, int* HighlightMin, int* WaterFogAlpha, float* DirLightMult)
	{
		constexpr int NUM_EXPECTED_PARAMS = 52;

		static int LastAmb_R, LastAmb_G, LastAmb_B;
		int CurAmb_R = 0, CurAmb_G = 0, CurAmb_B = 0;

		// Give the directional multiplier a graceful default
		*DirLightMult = 1.0f;

		const int result = orgSscanf(s, format, &CurAmb_R, &CurAmb_G, &CurAmb_B, Amb_Obj_R, Amb_Obj_G, Amb_Obj_B, Dir_R, Dir_G, Dir_B,
			SkyTop_R, SkyTop_G, SkyTop_B, SkyBot_R, SkyBot_G, SkyBot_B, SunCore_R, SunCore_G, SunCore_B, SunCorona_R, SunCorona_G, SunCorona_B,
			SunSz, SprSz, SprBght, Shdw, LightShd, PoleShd, FarClp, FogSt, LightOnGround, LowCloudsR, LowCloudsG, LowCloudsB,
			BottomCloudR, BottomCloudG, BottomCloudB, WaterR, WaterG, WaterB, WaterA, PostFX1_A, PostFX1_R, PostFX1_G, PostFX1_B,
			PostFX2_A, PostFX2_R, PostFX2_G, PostFX2_B, Cloud_A, HighlightMin, WaterFogAlpha, DirLightMult);

		// We expect the directional multiplier to be missing, but RAINY_COUNTRYSIDE is missing the first two values, and the third is corrupted.
		// So if at least 3 variables fail to parse, try the fallback
		if (result <= (NUM_EXPECTED_PARAMS-3))
		{
			// String format matching the broken line
			orgSscanf(s, "%*d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %f %f %f %d %d %d %f %f %f %d %d %d %d %d %d %f %f %f %f %f %f %f %f %f %f %f %f %f %d %d %f",
				Amb_Obj_R, Amb_Obj_G, Amb_Obj_B, Dir_R, Dir_G, Dir_B,
				SkyTop_R, SkyTop_G, SkyTop_B, SkyBot_R, SkyBot_G, SkyBot_B, SunCore_R, SunCore_G, SunCore_B, SunCorona_R, SunCorona_G, SunCorona_B,
				SunSz, SprSz, SprBght, Shdw, LightShd, PoleShd, FarClp, FogSt, LightOnGround, LowCloudsR, LowCloudsG, LowCloudsB,
				BottomCloudR, BottomCloudG, BottomCloudB, WaterR, WaterG, WaterB, WaterA, PostFX1_A, PostFX1_R, PostFX1_G, PostFX1_B,
				PostFX2_A, PostFX2_R, PostFX2_G, PostFX2_B, Cloud_A, HighlightMin, WaterFogAlpha, DirLightMult);

			CurAmb_R = LastAmb_R;
			CurAmb_G = LastAmb_G;
			CurAmb_B = LastAmb_B;
		}
		else
		{
			// Preserve the ambient values from a valid line
			LastAmb_R = CurAmb_R;
			LastAmb_G = CurAmb_G;
			LastAmb_B = CurAmb_B;
		}
		*Amb_R = CurAmb_R;
		*Amb_G = CurAmb_G;
		*Amb_B = CurAmb_B;

		return result;
	}
}


// ============= Stop gang wars clearing the friendly entity blips when the gang war ends =============
namespace AttackWaveFriendlyBlipsFix
{
	static bool HasGameBindings()
	{
		return EnsureBindings(IsInPlayersGroup);
	}

	static bool __fastcall ClearBlipsOnDeath_AndNotIsInPlayersGroup(const CPed* ped)
	{
		return ped->m_nPedFlags.bClearRadarBlipOnDeath && !IsInPlayersGroup.Call(ped);
	}
}


// ============= Speech system fixes =============
namespace SpeechSystemFixes
{
	static bool HasGameBindings_AllFixes()
	{
		return EnsureBindings(CPed::SayFunc);
	}

	static uint32_t* ConversationTopic;

	static bool (__thiscall* orgPedIsReadyForConversation)(CPed* ped, bool bCheckWantedLevel);
	static bool __fastcall PedIsReadyForConversation_CheckAlive(CPed* ped, void*, bool bCheckWantedLevel)
	{
		if (!ped->IsAlive() || ped->fHealth <= 0.1f)
		{
			return false;
		}
		return orgPedIsReadyForConversation(ped, bCheckWantedLevel);
	}

	static void* (__thiscall* orgPedSay_Weather)(void* ped, uint16_t Phrase, void* StartTimeDelay, void* Probability, void* bOverideSilence, void* bForceAudible, void* bFrontEnd);
	static void* __fastcall PedSay_NegativeWeatherOverride(void* ped, void*, uint16_t Phrase, void* StartTimeDelay, void* Probability, void* bOverideSilence, void* bForceAudible, void* bFrontEnd)
	{
		// Positive replies to a negative weather comment need special casing
		if ((Phrase == CONTEXT_GLOBAL_LIKE_DISMISS_FEMALE || Phrase == CONTEXT_GLOBAL_LIKE_DISMISS_MALE) && *ConversationTopic == AE_CONV_WEATHER)
		{
			Phrase = CONTEXT_GLOBAL_WEATHER_DISL_REPLY;
		}
		return orgPedSay_Weather(ped, Phrase, StartTimeDelay, Probability, bOverideSilence, bForceAudible, bFrontEnd);
	}

	static bool bShouldOverrideWellDressedMood = false;
	static int16_t (*orgGetCurrentCJMood)();
	static int16_t GetCurrentCJMood_Override()
	{
		return bShouldOverrideWellDressedMood ? 2 : orgGetCurrentCJMood(); // Force MOOD_CD
	}

	static int16_t GetSoundAndBankIDs_WeatherReplyFallback_Internal(int16_t (__thiscall* func)(CAEPedSpeechAudioEntity* obj, int16_t GlobalSpeechContext, void* a2),
					CAEPedSpeechAudioEntity* obj, int16_t GlobalSpeechContext, void* a2)
	{
		int16_t result = func(obj, GlobalSpeechContext, a2);

		// Fallback for player's weather replies - force a CD mood, since only CD and CF moods have responses, and since we cannot update
		// the speech lookup tables to reference samples across different moods, we need to fake the CD mood for the call.
		if (result < 0 && obj->m_PedType == PED_TYPE_PLAYER && (GlobalSpeechContext == CONTEXT_GLOBAL_WEATHER_DISL_REPLY || GlobalSpeechContext == CONTEXT_GLOBAL_WEATHER_LIKE_REPLY))
		{
			bShouldOverrideWellDressedMood = true;
			result = func(obj, GlobalSpeechContext, a2);
			bShouldOverrideWellDressedMood = false;
		}
		return result;
	}

	template<std::size_t Index>
	static int16_t (__thiscall* orgGetSoundAndBankIDs)(CAEPedSpeechAudioEntity* obj, int16_t GlobalSpeechContext, void* a2);
	template<std::size_t Index>
	static int16_t __fastcall GetSoundAndBankIDs_WeatherReplyFallback(CAEPedSpeechAudioEntity* obj, void*, int16_t GlobalSpeechContext, void* a2)
	{
		return GetSoundAndBankIDs_WeatherReplyFallback_Internal(orgGetSoundAndBankIDs<Index>, obj, GlobalSpeechContext, a2);
	}

	HOOK_EACH_INIT(GetSoundAndBankIDs, orgGetSoundAndBankIDs, GetSoundAndBankIDs_WeatherReplyFallback);

	static int16_t GetVoice_CheckTypos_Internal(int16_t (*func)(const char* pString, void* type), const char* pString, void* type)
	{
		int16_t result = func(pString, type);
		if (result < 0 && strcmp(pString, "0") != 0)
		{
			// Must be sorted by the second parameter
			static constexpr std::pair<const char*, const char*> typosAndFixes[] = {
				{ "VOICE_GEN_BYMPI", "VOICE_GEN_BMYPI" },
				{ "VOICE_GEN_WMYSGRD", "VOICE_GEN_WMYSGRAD" },
			};

			for (const auto& typo : typosAndFixes)
			{
				const int comp = strcmp(typo.second, pString);
				if (comp == 0)
				{
					return func(typo.first, type);
				}
				if (comp > 0) break;
			}
		}
		return result;
	}

	template<std::size_t Index>
	static int16_t (*orgGetVoice)(const char* pString, void* type);
	template<std::size_t Index>
	static int16_t GetVoice_CheckTypos(const char* pString, void* type)
	{
		return GetVoice_CheckTypos_Internal(orgGetVoice<Index>, pString, type);
	}

	HOOK_EACH_INIT(GetVoice, orgGetVoice, GetVoice_CheckTypos);

	template<std::size_t Index>
	static int (*orgStricmp)(const char* left, const char* right);
	template<std::size_t Index>
	static int stricmp_UseTextLabel(const char* left, const char* right)
	{
		return orgStricmp<Index>(left + 8, right);
	}

	HOOK_EACH_INIT(TextLabelStricmp, orgStricmp, stricmp_UseTextLabel);

	static int (*orgSscanf_PedsIde)(const char* s, const char* format, ...);
	static int sscanf_PedsIde_FixupVoiceNames(const char* s, const char* format, int* modelId, char* modelName, char* texName, char* pedType, char* statName, char* animGroup,
					int* carsCanDriveMask, int* flags, char* animFile, int* radio1, int* radio2, char* pedVoiceType, char* voice1, char* voice2)
	{
		int result = orgSscanf_PedsIde(s, format, modelId, modelName, texName, pedType, statName, animGroup, carsCanDriveMask, flags, animFile, radio1, radio2,
					pedVoiceType, voice1, voice2);
		if (result == 14)
		{
			// For S*FYSTR, we fix up the voice name if it's VOICE_GEN_S*FYST
			// Model name is case-insensitive, voice names are case-sensitive
			if (_stricmp(modelName, "sbfystr") == 0 && strcmp(voice1, "VOICE_GEN_SBFYST") == 0 && strcmp(voice2, "VOICE_GEN_SBFYST") == 0)
			{
				strcpy_s(voice1, 60, "VOICE_GEN_SBFYSTR");
				strcpy_s(voice2, 60, "VOICE_GEN_SBFYSTR");
			}
			else if (_stricmp(modelName, "swfystr") == 0 && strcmp(voice1, "VOICE_GEN_SWFYST") == 0 && strcmp(voice2, "VOICE_GEN_SWFYST") == 0)
			{
				strcpy_s(voice1, 60, "VOICE_GEN_SWFYSTR");
				strcpy_s(voice2, 60, "VOICE_GEN_SWFYSTR");
			}
		}

		return result;
	}


	static void (*orgSetCJMood)(void*, void*, void*, void*, void*);
	// Those are not actually void*, but we just need to store these arguments for later,
	// regardless of what they contain
	static void* nBasicMood_Stored;
	static void* nTimeInMilliseconds_Stored;
	static void* nGangBanging_Stored;
	static void* nFat_Stored;
	static void* nWellDressed_Stored;
	static void SetCJMood_Store(void* nBasicMood, void* nTimeInMilliseconds, void* nGangBanging, void* nFat, void* nWellDressed)
	{
		nBasicMood_Stored = nBasicMood;
		nTimeInMilliseconds_Stored = nTimeInMilliseconds;
		nGangBanging_Stored = nGangBanging;
		nFat_Stored = nFat;
		nWellDressed_Stored = nWellDressed;
	}
	static void* (__thiscall* orgPedSay_Solicit)(void* ped, void* Phrase, void* StartTimeDelay, void* Probability, void* bOverideSilence, void* bForceAudible, void* bFrontEnd);
	static void* __fastcall PedSay_Solicit_ChangeMood(void* ped, void*, void* Phrase, void* StartTimeDelay, void* Probability, void* bOverideSilence, void* bForceAudible, void* bFrontEnd)
	{
		void* result = orgPedSay_Solicit(ped, Phrase, StartTimeDelay, Probability, bOverideSilence, bForceAudible, bFrontEnd);
		orgSetCJMood(nBasicMood_Stored, nTimeInMilliseconds_Stored, nGangBanging_Stored, nFat_Stored, nWellDressed_Stored);
		return result;
	}

	static bool HasGameBindings_DoGangAbuseSpeech()
	{
		return HasGameBindings_AllFixes();
	}

	static bool DoGangAbuseSpeech_LSV(CPed* ped1, CPed* ped2)
	{
		if (ped1->GetPedType() >= PEDTYPE_GANG1 && ped1->GetPedType() <= PEDTYPE_GANG10 && ped2->GetPedType() == PEDTYPE_GANG3)
		{
			ped1->Say(CONTEXT_GLOBAL_ABUSE_GANG_LSV);
			return true;
		}
		return false;
	}

	template<std::size_t Index>
	static void (*orgDoGangAbuseSpeech)(CPed* ped1, CPed* ped2);
	template<std::size_t Index>
	static void DoGangAbuseSpeech_AddLSV(CPed* ped1, CPed* ped2)
	{
		if (!DoGangAbuseSpeech_LSV(ped1, ped2))
		{
			orgDoGangAbuseSpeech<Index>(ped1, ped2);
		}
	}

	HOOK_EACH_INIT(DoGangAbuseSpeech, orgDoGangAbuseSpeech, DoGangAbuseSpeech_AddLSV);

	static bool HasGameBindings_DoGangAttackSpeech()
	{
		return HasGameBindings_AllFixes();
	}

	static void (*orgDoGangAttackSpeech)(CPed* ped1, CPed* ped2);
	static void DoGangAttackSpeech_AddPlayer(CPed* ped1, CPed* ped2)
	{
		if (ped1 != nullptr && ped2 != nullptr)
		{
			if (ped1->GetPedType() >= PEDTYPE_GANG1 && ped1->GetPedType() <= PEDTYPE_GANG10 && ped2->IsPlayer())
			{
				ped1->Say(CONTEXT_GLOBAL_ATTACK_PLAYER);
				return;
			}
		}
		orgDoGangAttackSpeech(ped1, ped2);
	}

	static int16_t PedSay_VehicleDamageFallback_Internal(int16_t (__thiscall* func)(void* ped, uint16_t Phrase, void* StartTimeDelay, void* Probability, void* bOverideSilence, void* bForceAudible, void* bFrontEnd),
		void* ped, uint16_t Phrase, void* StartTimeDelay, void* Probability, void* bOverideSilence, void* bForceAudible, void* bFrontEnd)
	{
		int16_t result = func(ped, Phrase, StartTimeDelay, Probability, bOverideSilence, bForceAudible, bFrontEnd);
		if (result < 0 && Phrase != CONTEXT_GLOBAL_CRASH_GENERIC)
		{
			result = func(ped, CONTEXT_GLOBAL_CRASH_GENERIC, StartTimeDelay, Probability, bOverideSilence, bForceAudible, bFrontEnd);
		}
		return result;
	}

	template<std::size_t Index>
	static int16_t (__thiscall* orgPedSay_VehicleDamage)(void* ped, uint16_t Phrase, void* StartTimeDelay, void* Probability, void* bOverideSilence, void* bForceAudible, void* bFrontEnd);
	template<std::size_t Index>
	static int16_t __fastcall PedSay_VehicleDamageFallback(void* ped, void*, uint16_t Phrase, void* StartTimeDelay, void* Probability, void* bOverideSilence, void* bForceAudible, void* bFrontEnd)
	{
		return PedSay_VehicleDamageFallback_Internal(orgPedSay_VehicleDamage<Index>, ped, Phrase, StartTimeDelay, Probability, bOverideSilence, bForceAudible, bFrontEnd);
	}

	HOOK_EACH_INIT(VehicleDamageFallback, orgPedSay_VehicleDamage, PedSay_VehicleDamageFallback);

	static bool HasGameBindings_PainSprayed()
	{
		return HasGameBindings_AllFixes() && EnsureBindings(CPedIntelligence::GetTaskUseGun);
	}

	static bool CheckAndPlayPainSprayed(CPed* ped, const CPedDamageResponseCalculator* damage)
	{
		if (damage->m_eWeaponUsed == WEAPONTYPE_TEARGAS || damage->m_eWeaponUsed == WEAPONTYPE_SPRAYCAN || damage->m_eWeaponUsed == WEAPONTYPE_EXTINGUISHER)
		{
			// Bail out if the victim was "whipped" with the can, and not sprayed
			if (damage->m_pInflictor != nullptr && damage->m_pInflictor->GetType() == ENTITY_TYPE_PED)
			{
				const CTaskSimpleUseGun* useGunTask = CPedIntelligence::GetTaskUseGun.Call(static_cast<const CPed*>(damage->m_pInflictor)->GetPedIntelligence());
				if (useGunTask != nullptr && useGunTask->GetCurrentCommand() == GCOMMAND_PISTOLWHIP)
				{
					return false;
				}
			}

			ped->Say(CONTEXT_GLOBAL_PAIN_SPRAYED);
			return true;
		}
		return false;
	}

	static void* CheckForSprayHook_JumpBack;
	static __declspec(naked) void CheckForSprayHook_OldBinaries()
	{
		static const float FIVE = 5.0f;
		_asm
		{
			push	ebp
			push	esi
			call	CheckAndPlayPainSprayed
			add		esp, 8
			test	al, al
			jz		CheckForSprayHook_Return

			// Original epilogue
			pop		edi
			pop		esi
			pop		ebp
			pop		ebx
			retn	0Ch

		CheckForSprayHook_Return:
			// Original code
			fld		dword ptr [ebp+4]
			fcomp	FIVE
			jmp		[CheckForSprayHook_JumpBack]
		}
	}

	static int16_t (__thiscall* orgPedSay_ArrestPed)(CPed* ped, uint16_t Phrase, uint32_t StartTimeDelay, float Probability, void* bOverideSilence, void* bForceAudible, void* bFrontEnd);
	static int16_t __fastcall PedSay_ArrestPed_CJReply(CPed* cop, CPed* player, uint16_t Phrase, uint32_t StartTimeDelay, float Probability, void* bOverideSilence, void* bForceAudible, void* bFrontEnd)
	{
		const int16_t result = orgPedSay_ArrestPed(cop, Phrase, StartTimeDelay, Probability, bOverideSilence, bForceAudible, bFrontEnd);
		if (result >= 0 && player->GetCurrentVehicle() == nullptr) // Some lines specifically refer to running, so they only make sense on-foot.
		{
			// The above SOLO is very frequent, so reduce the probability of this response a bit
			orgPedSay_ArrestPed(player, CONTEXT_GLOBAL_CHASED, 3500, 0.6f, bOverideSilence, bForceAudible, bFrontEnd);
		}
		return result;
	}

	static __declspec(naked) void PedSay_ArrestPedHook()
	{
		_asm
		{
			mov		edx, dword ptr [esi+10h]
			jmp		PedSay_ArrestPed_CJReply
		}
	}

	static CEntity* s_targetEntity = nullptr;
	static int16_t (__thiscall* orgPedSay_ShootContext)(void* ped, void* Phrase, void* StartTimeDelay, void* Probability, void* bOverideSilence, void* bForceAudible, void* bFrontEnd);
	static int16_t __fastcall PedSay_ShootContext_CheckTarget(CPlayerPed* firedBy, void*, void* Phrase, void* StartTimeDelay, void* Probability, void* bOverideSilence, void* bForceAudible, void* bFrontEnd)
	{
		CEntity* targetEntity = std::exchange(s_targetEntity, nullptr);
		if (targetEntity == nullptr)
		{
			targetEntity = firedBy->GetMouseLockOnRecruitPed();
		}
		if (targetEntity != nullptr && targetEntity->GetType() == ENTITY_TYPE_PED)
		{
			return orgPedSay_ShootContext(firedBy, Phrase, StartTimeDelay, Probability, bOverideSilence, bForceAudible, bFrontEnd);
		}
		return -1;

	}

	static CWanted* (*orgFindPlayerWanted_GetPedsGroup)(int playerID);
	static void* GetPedsGroup_DummyForCops()
	{
		return orgFindPlayerWanted_GetPedsGroup(-1)->m_nCopsInPursuit > 1 ? &__ImageBase : nullptr;
	}

	template<std::size_t Index>
	static void* (*orgGetPedsGroup)(const CPed* pPed);
	template<std::size_t Index>
	static void* GetPedsGroup_WithDummyForCops(const CPed* pPed)
	{
		if (pPed->GetPedType() == PEDTYPE_COP)
		{
			return GetPedsGroup_DummyForCops();
		}
		return orgGetPedsGroup<Index>(pPed);
	}

	HOOK_EACH_INIT(GetPedGroupsForCops, orgGetPedsGroup, GetPedsGroup_WithDummyForCops);

	static int16_t (__thiscall* orgPedSay_DismissReply)(void* ped, uint16_t Phrase, void* StartTimeDelay, void* Probability, void* bOverideSilence, void* bForceAudible, void* bFrontEnd);
	static int16_t __fastcall PedSay_DismissReply_WhereYouFrom(void* ped, void*, uint16_t Phrase, void* StartTimeDelay, void* Probability, void* bOverideSilence, void* bForceAudible, void* bFrontEnd)
	{
		const uint32_t Topic = *ConversationTopic;

		// This code also runs for AE_CONV_GANGBANG, but it "shouldn't"
		if (Topic == AE_CONV_GANGBANG)
		{
			return -1;
		}

		if (Topic == AE_CONV_WHERE_YOU_FROM)
		{
			Phrase = CONTEXT_GLOBAL_WHERE_YOU_FROM_POS_REPLY;
		}
		return orgPedSay_DismissReply(ped, Phrase, StartTimeDelay, Probability, bOverideSilence, bForceAudible, bFrontEnd);
	}

	static void* orgPedSay_MakeThisPedJoinOurGroup_DruggedIgnore;
	static __declspec(naked) void PedSay_MakeThisPedJoinOurGroup_DruggedIgnore()
	{
		_asm
		{
			mov		ecx, edi
			jmp		[orgPedSay_MakeThisPedJoinOurGroup_DruggedIgnore]
		}
	}

	namespace Patches
	{
		static void PatchGlobalSpeechContexts(int16_t (*gSpeechContextLookup)[8])
		{
			auto setSpecificSpeechContext = [gSpeechContextLookup](int16_t globalContext, int16_t pedAudioType, int16_t specificContext)
				{
					for (size_t i = 0; gSpeechContextLookup[i][0] != -1; ++i)
					{
						if (gSpeechContextLookup[i][0] == globalContext)
						{
							if (gSpeechContextLookup[i][pedAudioType + 1] == -1)
							{
								gSpeechContextLookup[i][pedAudioType + 1] = specificContext;
							}
							return;
						}
					}
				};

			// Re-enable CJ's turf takeover cheer speech contexts
			setSpecificSpeechContext(CONTEXT_GLOBAL_TAKE_TURF_LAS_COLINAS, PED_TYPE_PLAYER, CONTEXT_PLY_TAKE_TURF_LAS_COLINAS);
			setSpecificSpeechContext(CONTEXT_GLOBAL_TAKE_TURF_LOS_FLORES, PED_TYPE_PLAYER, CONTEXT_PLY_TAKE_TURF_LOS_FLORES);
			setSpecificSpeechContext(CONTEXT_GLOBAL_TAKE_TURF_EAST_BEACH, PED_TYPE_PLAYER, CONTEXT_PLY_TAKE_TURF_EAST_BEACH);
			setSpecificSpeechContext(CONTEXT_GLOBAL_TAKE_TURF_EAST_LS, PED_TYPE_PLAYER, CONTEXT_PLY_TAKE_TURF_EAST_LS);
			setSpecificSpeechContext(CONTEXT_GLOBAL_TAKE_TURF_JEFFERSON, PED_TYPE_PLAYER, CONTEXT_PLY_TAKE_TURF_JEFFERSON);
			setSpecificSpeechContext(CONTEXT_GLOBAL_TAKE_TURF_GLEN_PARK, PED_TYPE_PLAYER, CONTEXT_PLY_TAKE_TURF_GLEN_PARK);
			setSpecificSpeechContext(CONTEXT_GLOBAL_TAKE_TURF_IDLEWOOD, PED_TYPE_PLAYER, CONTEXT_PLY_TAKE_TURF_IDLEWOOD);
			setSpecificSpeechContext(CONTEXT_GLOBAL_TAKE_TURF_GANTON, PED_TYPE_PLAYER, CONTEXT_PLY_TAKE_TURF_GANTON);
			setSpecificSpeechContext(CONTEXT_GLOBAL_TAKE_TURF_LITTLE_MEXICO, PED_TYPE_PLAYER, CONTEXT_PLY_TAKE_TURF_LITTLE_MEXICO);
			setSpecificSpeechContext(CONTEXT_GLOBAL_TAKE_TURF_WILLOWFIELD, PED_TYPE_PLAYER, CONTEXT_PLY_TAKE_TURF_WILLOWFIELD);
			setSpecificSpeechContext(CONTEXT_GLOBAL_TAKE_TURF_PLAYA_DEL_SEVILLE, PED_TYPE_PLAYER, CONTEXT_PLY_TAKE_TURF_PLAYA_DEL_SEVILLE);
			setSpecificSpeechContext(CONTEXT_GLOBAL_TAKE_TURF_TEMPLE, PED_TYPE_PLAYER, CONTEXT_PLY_TAKE_TURF_TEMPLE);

			// Those contexts have meaningful content for girlfriends and shopkeepers, but are unassigned
			setSpecificSpeechContext(CONTEXT_GLOBAL_CONV_IGNORED, PED_TYPE_GIRLFRIEND, CONTEXT_GFD_CONV_IGNORED);
			setSpecificSpeechContext(CONTEXT_GLOBAL_GUN_COOL, PED_TYPE_GIRLFRIEND, CONTEXT_GFD_GUN_COOL);
			setSpecificSpeechContext(CONTEXT_GLOBAL_LIKE_DISMISS_REPLY, PED_TYPE_GIRLFRIEND, CONTEXT_GFD_LIKE_DISMISS_REPLY);
			setSpecificSpeechContext(CONTEXT_GLOBAL_PCONV_AGREE_BAD, PED_TYPE_GIRLFRIEND, CONTEXT_GFD_PCONV_AGREE_BAD);
			setSpecificSpeechContext(CONTEXT_GLOBAL_PCONV_AGREE_GOOD, PED_TYPE_GIRLFRIEND, CONTEXT_GFD_PCONV_AGREE_GOOD);
			setSpecificSpeechContext(CONTEXT_GLOBAL_PCONV_ANS_NO, PED_TYPE_GIRLFRIEND, CONTEXT_GFD_PCONV_ANS_NO);
			setSpecificSpeechContext(CONTEXT_GLOBAL_PCONV_DISMISS, PED_TYPE_GIRLFRIEND, CONTEXT_GFD_PCONV_DISMISS);
			setSpecificSpeechContext(CONTEXT_GLOBAL_PCONV_GREET_FEM, PED_TYPE_GIRLFRIEND, CONTEXT_GFD_PCONV_GREET_FEM);
			setSpecificSpeechContext(CONTEXT_GLOBAL_PCONV_GREET_MALE, PED_TYPE_GIRLFRIEND, CONTEXT_GFD_PCONV_GREET_MALE);
			setSpecificSpeechContext(CONTEXT_GLOBAL_PCONV_PART_FEM, PED_TYPE_GIRLFRIEND, CONTEXT_GFD_PCONV_PART_FEM);
			setSpecificSpeechContext(CONTEXT_GLOBAL_PCONV_PART_MALE, PED_TYPE_GIRLFRIEND, CONTEXT_GFD_PCONV_PART_MALE);
			setSpecificSpeechContext(CONTEXT_GLOBAL_PCONV_QUESTION, PED_TYPE_GIRLFRIEND, CONTEXT_GFD_PCONV_QUESTION);
			setSpecificSpeechContext(CONTEXT_GLOBAL_PCONV_STATE_BAD, PED_TYPE_GIRLFRIEND, CONTEXT_GFD_PCONV_STATE_BAD);
			setSpecificSpeechContext(CONTEXT_GLOBAL_PCONV_STATE_GOOD, PED_TYPE_GIRLFRIEND, CONTEXT_GFD_PCONV_STATE_GOOD);
			setSpecificSpeechContext(CONTEXT_GLOBAL_SAVED, PED_TYPE_GIRLFRIEND, CONTEXT_GFD_SAVED);
			setSpecificSpeechContext(CONTEXT_GLOBAL_SHOCKED, PED_TYPE_GIRLFRIEND, CONTEXT_GFD_SHOCKED);
			setSpecificSpeechContext(CONTEXT_GLOBAL_TRAPPED, PED_TYPE_GIRLFRIEND, CONTEXT_GFD_TRAPPED);
			setSpecificSpeechContext(CONTEXT_GLOBAL_GUN_RUN, PED_TYPE_GIRLFRIEND, CONTEXT_GFD_GUN_RUN);

			// CONTEXT_GLOBAL_SHOOT_GENERIC for girlfriends/shopkeepers and pedestrians
			// Currently unused, but this way another mod can enable them
			setSpecificSpeechContext(CONTEXT_GLOBAL_SHOOT_GENERIC, PED_TYPE_GEN, CONTEXT_GEN_SHOOT_GENERIC);
			setSpecificSpeechContext(CONTEXT_GLOBAL_SHOOT_GENERIC, PED_TYPE_GIRLFRIEND, CONTEXT_GFD_SHOOT_GENERIC);

			// Unused gang taunts
			setSpecificSpeechContext(CONTEXT_GLOBAL_ABUSE_TRIAD, PED_TYPE_GANG, CONTEXT_GNG_ABUSE_GANG_TRIADS);
			setSpecificSpeechContext(CONTEXT_GLOBAL_ABUSE_RIFA, PED_TYPE_GANG, CONTEXT_GNG_ABUSE_GANG_SFR);
			setSpecificSpeechContext(CONTEXT_GLOBAL_ABUSE_DA_NANG, PED_TYPE_GANG, CONTEXT_GNG_ABUSE_GANG_DNB);
		}

		template<typename T>
		static void tryMoveContext(T* speechLookup, size_t voiceID, int16_t source, int16_t destination)
		{
			auto& srcVoice = speechLookup[source][voiceID];
			auto& destVoice = speechLookup[destination][voiceID];
			constexpr auto NO_VOICE = static_cast<std::decay_t<decltype(srcVoice[0])>>(-1);
			if (srcVoice[0] != NO_VOICE && srcVoice[1] != NO_VOICE && destVoice[0] == NO_VOICE && destVoice[1] == NO_VOICE)
			{
				destVoice[0] = std::exchange(srcVoice[0], NO_VOICE);
				destVoice[1] = std::exchange(srcVoice[1], NO_VOICE);
			}
		}

		static void PatchGenSpeechContexts(uint8_t (*gGenSpeechLookup)[209][2])
		{
			// Copy typo'd speech contexts "caused" by VBFYST2 to the correct contexts
			for (size_t i = 0; i < std::size(*gGenSpeechLookup); i++)
			{
				tryMoveContext(gGenSpeechLookup, i, CONTEXT_GEN_CHA, CONTEXT_GEN_CHAT);
				tryMoveContext(gGenSpeechLookup, i, CONTEXT_GEN_RUN_FROM_FIGH, CONTEXT_GEN_RUN_FROM_FIGHT);
				tryMoveContext(gGenSpeechLookup, i, CONTEXT_GEN_SAVE, CONTEXT_GEN_SAVED);
				tryMoveContext(gGenSpeechLookup, i, CONTEXT_GEN_SHOCKE, CONTEXT_GEN_SHOCKED);
			}

			// VWMOTR1 has a mis-assigned MUGGED context
			tryMoveContext(gGenSpeechLookup, VOICE_GEN_VWMOTR1, CONTEXT_GEN_MUGGING, CONTEXT_GEN_MUGGED);
		}

		static void PatchEmgSpeechContexts(int16_t (*gEmgSpeechLookup)[46][2])
		{
			// Copy typo'd speech contexts to the correct contexts
			for (size_t i = 0; i < std::size(*gEmgSpeechLookup); i++)
			{
				tryMoveContext(gEmgSpeechLookup, i, CONTEXT_EMG_ARREST_CRIMINAL, CONTEXT_EMG_ARREST_CRIM);
			}
		}

		static void PatchGfdSpeechContexts(int16_t (*gGfdSpeechLookup)[18][2])
		{
			// CONTEXT_GFD_SHOOT_GENERIC contains data meaningful for CONTEXT_GFD_SHOP_CLOSED,
			// so moving it restores lines for barbers/AmmuNation keepers when they are spooked.
			// CONTEXT_GFD_CAR_ON_FIRE is an incorrect context.
			for (size_t i = 0; i < std::size(*gGfdSpeechLookup); i++)
			{
				tryMoveContext(gGfdSpeechLookup, i, CONTEXT_GFD_SHOOT_GENERIC, CONTEXT_GFD_SHOP_CLOSED);
				tryMoveContext(gGfdSpeechLookup, i, CONTEXT_GFD_CAR_ON_FIRE, CONTEXT_GFD_CAR_FIRE);
			}
		}

		static void PatchPlySpeechContexts(int16_t (*gPlySpeechLookup)[20][2])
		{
			// Copy typo'd speech contexts to the correct contexts
			for (size_t i = 0; i < std::size(*gPlySpeechLookup); i++)
			{
				tryMoveContext(gPlySpeechLookup, i, CONTEXT_PLY_JACKING_CAR_FEMALE, CONTEXT_PLY_JACKING_CAR_FEM);
			}
		}

		static void PatchGngSpeechContexts(int16_t (*gGngSpeechLookup)[52][2])
		{
			// Copy typo'd speech contexts to the correct contexts
			for (size_t i = 0; i < std::size(*gGngSpeechLookup); i++)
			{
				tryMoveContext(gGngSpeechLookup, i, CONTEXT_GNG_ATTACK_PLAYER_LIKE, CONTEXT_GNG_ATTACK_BY_PLAYER_LIKE);
			}
		}

		static void PatchPedVoiceNameLookups(int16_t (*gSpecialPedVoiceLookup)[3], char (*gSpecialPedVoiceNameLookup)[20], size_t numPedVoiceLookups)
		{
			for (size_t i = 0; i < numPedVoiceLookups; i++)
			{
				auto& voice = gSpecialPedVoiceLookup[i];
				if (voice[0] == -1 && voice[1] == -1)
				{
					strncpy_s(gSpecialPedVoiceNameLookup[i], "maccer", sizeof(gSpecialPedVoiceNameLookup[i]));

					voice[0] = VOICE_GNG_MACCER;
					voice[1] = PED_TYPE_GANG;
					voice[2] = 0;
					return;
				}
			}
		}
	}
}


// ============= Good Citizen Bonus =============
namespace GoodCitizenBonus
{
	static bool HasGameBindings()
	{
		return CPed::SayFunc.Ensure();
	}

	static CPed* pChasingCopPed;
	bool __stdcall IsPedFleeingFromCops_PlayAudio(const CPed* ped)
	{
		// If this flag is set, we already gave a bonus for this ped once
		// Don't check for the event again
		if (ped->m_nPedFlags.bBeingChasedByPolice)
		{
			pChasingCopPed = nullptr;
			return true;
		}

		CEvent* event = ped->GetPedIntelligence()->GetCurrentEvent();
		if (event != nullptr && event->GetEventType() == CEvent::EVENT_PED_TO_FLEE)
		{
			CEventPedToFlee* eventPedToFlee = static_cast<CEventPedToFlee*>(event);
			CPed* chasingCopPed = eventPedToFlee->GetPedToFlee();
			if (chasingCopPed != nullptr && chasingCopPed->GetPedType() == PEDTYPE_COP)
			{
				pChasingCopPed = chasingCopPed;
				return true;
			}
		}

		return false;
	}

	bool __stdcall IsNotDying_AndBonusNotGiven(CPed* ped)
	{
		// If we already gave the bonus, don't give it again
		if (ped->m_nPedFlags.bBeingChasedByPolice)
		{
			return false;
		}

		int pedState = ped->GetPedState();
		if (pedState != 54 && pedState != 55) // != PEDSTATE_DIE && != PEDSTATE_DEAD
		{
			ped->m_nPedFlags.bBeingChasedByPolice = true;
			if (pChasingCopPed != nullptr)
			{
				pChasingCopPed->Say(CONTEXT_GLOBAL_GOOD_CITIZEN);
			}
			return true;
		}
		return false;
	}

	const char* (__thiscall *orgGetText_Goodboy)(void* obj, void* key);
	const char* __fastcall GetText_Goodboy_Fallback(void* obj, void*, void* key)
	{
		const char* text = orgGetText_Goodboy(obj, key);
		if (text[0] == '\0')
		{
			text = "$50 Good Citizen Bonus!";
		}
		return text;
	}
}


// ============= Fix CShadows::CastShadowEntityXY ignoring the Up rotation of an object =============
namespace CastShadowEntityFix
{
	void __fastcall TransformShadowPoint3D(const CEntity* entity, CVector* vec)
	{
		*vec = *entity->GetMatrix() * *vec;
	}
}


// ============= Fix script draws affecting the line wrapping of a radio station name display =============
namespace RadioStationDisplayWidth
{
	static bool HasGameBindings()
	{
		return UIScales::HasGameBindings();
	}

	static void (*SetCentreSize)(float x);

	static void (*orgSetDropColor)(void* color);
	static void SetDropColor_AndCentreSize(void* color)
	{
		orgSetDropColor(color);
		SetCentreSize(static_cast<float>(RsGlobal.Get().MaximumWidth));
	}
}


// ============= Intercept binary IPL car generator spawns to fix a Beagle spawn in Fort Carson =============
namespace FortCarsonBeagle
{
	// This is technically an asset bug, but we can do a binary match on the exact spawning coordinates of that broken Beagle
	// and fix it up with zero risk of breaking other mods.
	static void (*orgLoadCarGenerator)(void* pFileCarGen, void* level);
	static void LoadCarGenerator_FixupBeagle(void* pFileCarGen, void* level)
	{
		// XYZ + rotation + modelID of that Beagle
		static const uint8_t beagleCargen[] = { 0xDE, 0xD3, 0x1A, 0xC2, 0x66, 0x66, 0x87, 0x44, 0x63, 0x7F, 0x99, 0x41, 0xD5, 0xB8, 0xB2, 0x3D, 0xFF, 0x01, 0x00, 0x00 };
		if (memcmp(beagleCargen, pFileCarGen, sizeof(beagleCargen)) == 0)
		{
			int32_t* modelID = reinterpret_cast<int32_t*>(static_cast<char*>(pFileCarGen) + 0x10);
			// Replace it with a BMX
			*modelID = 481;
		}
		orgLoadCarGenerator(pFileCarGen, level);
	}
}


// ============= Clip the cursor to the game window bounds =============
namespace ClipCursorToGameWindow
{
	static bool bWindowActive = false, bWantsCursorClip = false, bCursorIsClipped = false;

	static void ConfineCursor()
	{
		if (!bCursorIsClipped)
		{
			HWND window = RsGlobal.Get().ps->window;
			if (window != nullptr)
			{
				RECT clientRect;
				GetClientRect(window, &clientRect);

				// Make the coordinates inclusive, so grabbing the right/bottom side of the screen is not possible
				// (happens on high DPI displays otherwise)
				clientRect.right -= 1;
				clientRect.bottom -= 1;

				MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&clientRect), 2);

				bCursorIsClipped = ClipCursor(&clientRect) != FALSE;
			}
		}
	}

	static void UnconfineCursor()
	{
		if (bCursorIsClipped)
		{
			ClipCursor(nullptr);
			bCursorIsClipped = false;
		}
	}

	static WNDPROC* orgWindowProc;
	static LRESULT CALLBACK ClipWindowProcA(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (uMsg)
		{
		case WM_ACTIVATE:
			bWindowActive = bWantsCursorClip = LOWORD(wParam) != WA_INACTIVE;
			if (wParam == FALSE)
			{
				UnconfineCursor();
			}
			break;

			// If the window moves/resizes, we want it to unconfine and automatically re-confine at the next opportunity
		case WM_ENTERSIZEMOVE:
			bWantsCursorClip = false;
			UnconfineCursor();
			break;
		case WM_EXITSIZEMOVE:
			if (bWindowActive) bWantsCursorClip = true;
			UnconfineCursor();
			break;

		case WM_WINDOWPOSCHANGED:
		case WM_DISPLAYCHANGE:
			UnconfineCursor();
			break;
		}

		return (*orgWindowProc)(hWnd, uMsg, wParam, lParam);
	}
	static auto* const pClipWindowProcA = &ClipWindowProcA;

	static void DoClipCursor_InGame()
	{
		if (bWantsCursorClip)
		{
			ConfineCursor();
		}
	}

	static void DoClipCursor_InMenu()
	{
		UnconfineCursor();
	}

	static bool HasGameBindings()
	{
		return EnsureBindings(RsGlobal, bIsFrontEndActive);
	}

	static void (*orgRsCameraShowRaster)(void* camera);
	static void RsCameraShowRaster_ProcessCursorClip(void* camera)
	{
		if (bIsFrontEndActive.Get())
		{
			DoClipCursor_InMenu();
		}
		else
		{
			DoClipCursor_InGame();
		}

		orgRsCameraShowRaster(camera);
	}
}


// ============= Allow to store trains in garages =============
namespace TrainsInGarages
{
	static bool HasGameBindings()
	{
		return EnsureBindings(IsTrainModel, CTrainCtor);
	}

	CAutomobile* (__thiscall* orgCAutomobileCtor)(void*, int modelID, uint8_t createdBy, void* setupSuspensionLines);
	CVehicle* __fastcall CAutomobileCtor_OrTrain(CVehicle* obj, void*, int modelID, uint8_t createdBy, void* setupSuspensionLines)
	{
		if (IsTrainModel.Call(modelID))
		{
			CTrain* train = CTrainCtor.Call(static_cast<CTrain*>(obj), modelID, createdBy);

			// We need to mark it as derailed, or else the train can't exist outside of the tracks
			train->m_nTrainFlags.bDerailed = true;
			train->m_nPhysicalFlags.bInfiniteMass = false;
			train->m_nPhysicalFlags.bDontProcessCollisionOurSelves = false;
			return train;
		}
		return orgCAutomobileCtor(obj, modelID, createdBy, setupSuspensionLines);
	}
}


// ============= LS-RP Mode stuff =============
namespace LSRPMode
{
	struct IPv4
	{
		uint8_t ip[4];
		uint16_t port;

		friend bool operator == ( const IPv4& left, const IPv4& right )
		{
			return std::make_tuple( left.ip[0], left.ip[1], left.ip[2], left.ip[3] ) == std::make_tuple( right.ip[0], right.ip[1], right.ip[2], right.ip[3] ) &&
					( left.port == right.port || left.port == 0 || right.port == 0 );
		}
	};

	std::vector <IPv4> serversLSRPMode = {
		{ 149, 56, 123, 148, 7777 }, // LS-RP
		{ 198, 27, 95, 178, 7777 }, // AD:RP
	};

	bool ModeForced = false;
	void DetectPlayingOnLSRP()
	{
		IPv4 myIP = {};

		// Obtain IP and check if it's LS-RP
		int numArgs = 0;
		LPWSTR* cmdLine = CommandLineToArgvW( GetCommandLineW(), &numArgs );
		if ( cmdLine != nullptr )
		{
			for ( auto it = cmdLine + 1, end = cmdLine + numArgs; it != end; ++it )
			{
				if ( _wcsicmp( *it, L"-h" ) == 0 )
				{
					auto ipIt = std::next( it );
					if ( ipIt != end )
					{
						swscanf_s( *ipIt, L"%" SCNu8 ".%" SCNu8 ".%" SCNu8 ".%" SCNu8, &myIP.ip[0], &myIP.ip[1], &myIP.ip[2], &myIP.ip[3] );
						it = ipIt;
					}
					continue;
				}

				if ( _wcsicmp( *it, L"-p") == 0 )
				{
					auto portIt = std::next( it );
					if ( portIt != end )
					{
						swscanf_s( *portIt, L"%" SCNu16 , &myIP.port );
						it = portIt;
					}
					continue;
				}
			}

			LocalFree( cmdLine );
		}

		ModeForced = std::find( serversLSRPMode.begin(), serversLSRPMode.end(), myIP ) != serversLSRPMode.end();
	}

	void ReadServersList(const wchar_t* pPath)
	{
		constexpr size_t SCRATCH_PAD_SIZE = 32767;
		WideDelimStringReader reader( SCRATCH_PAD_SIZE );

		GetPrivateProfileSectionW( L"LSRPModeServers", reader.PutBuffer(), reader.GetSize(), pPath );
		while ( const wchar_t* str = reader.GetString() )
		{
			int ip[4];
			int port = 0;

			// IP is mandatory, port is optional
			const int argsRead = swscanf_s(str, L"%d.%d.%d.%d:%d", &ip[0], &ip[1], &ip[2], &ip[3], &port);
			if (argsRead >= 4)
			{
				IPv4 myIP;
				bool validIP = true;

				for ( size_t i = 0; i < 4; i++ )
				{
					if ( ip[i] >= 0 && ip[i] <= UINT8_MAX )
					{
						myIP.ip[i] = static_cast<uint8_t>(ip[i]);
					}
					else
					{
						validIP = false;
						break;
					}
				}

				if ( port >= 0 && port <= UINT16_MAX )
				{
					myIP.port = static_cast<uint16_t>(port);
				}
				else
				{
					validIP = false;
				}

				if ( validIP )
				{
					serversLSRPMode.emplace_back( myIP );
				}
			}
		}
	}
}

static bool IgnoresWeaponPedsForPCFix()
{
	// TODO: Pre-emptively add INI option to save hassle in the future
	return LSRPMode::ModeForced;
}


namespace ModelIndicesReadyHook
{
	static void (*orgMatchAllModelStrings)();
	static void MatchAllModelStrings_ReadySVF()
	{
		orgMatchAllModelStrings();
		SVF::MarkModelNamesReady();
	}
}

#ifndef NDEBUG

// ============= QPC spoof for verifying high timer issues =============
namespace FakeQPC
{
	static int64_t AddedTime;
	static BOOL WINAPI FakeQueryPerformanceCounter(PLARGE_INTEGER lpPerformanceCount)
	{
		const BOOL result = ::QueryPerformanceCounter( lpPerformanceCount );
		lpPerformanceCount->QuadPart += AddedTime;
		return result;
	}
}

#endif

#if MEM_VALIDATORS

#include <intrin.h>

// Validator for static allocations
void PutStaticValidator( uintptr_t begin, uintptr_t end )
{
	uint8_t* a = (uint8_t*)begin;
	uint8_t* b = (uint8_t*)end;

	std::fill( a, b, uint8_t(0xCC) );
}

void* malloc_validator(size_t size)
{
	return _malloc_dbg( size, _NORMAL_BLOCK, "EXE", (uintptr_t)_ReturnAddress() );
}

void* realloc_validator(void* ptr, size_t size)
{
	return _realloc_dbg( ptr, size, _NORMAL_BLOCK, "EXE", (uintptr_t)_ReturnAddress() );
}

void* calloc_validator(size_t count, size_t size)
{
	return _calloc_dbg( count, size, _NORMAL_BLOCK, "EXE", (uintptr_t)_ReturnAddress() );
}

void free_validator(void* ptr)
{
	_free_dbg(ptr, _NORMAL_BLOCK);
}

size_t _msize_validator(void* ptr)
{
	return _msize_dbg(ptr, _NORMAL_BLOCK);
}

void* _new(size_t size)
{
	return _malloc_dbg( size, _NORMAL_BLOCK, "EXE", (uintptr_t)_ReturnAddress() );
}

void _delete(void* ptr)
{
	_free_dbg(ptr, _NORMAL_BLOCK);
}

class CDebugMemoryMgr
{
public:
	static void* Malloc(size_t size)
	{
		return _malloc_dbg( size, _NORMAL_BLOCK, "EXE", (uintptr_t)_ReturnAddress() );
	}

	static void Free(void* ptr)
	{
		_free_dbg(ptr, _NORMAL_BLOCK);
	}

	static void* Realloc(void* ptr, size_t size)
	{
		return _realloc_dbg( ptr, size, _NORMAL_BLOCK, "EXE", (uintptr_t)_ReturnAddress() );
	}

	static void* Calloc(size_t count, size_t size)
	{
		return _calloc_dbg( count, size, _NORMAL_BLOCK, "EXE", (uintptr_t)_ReturnAddress() );
	}

	static void* MallocAlign(size_t size, size_t align)
	{
		return _aligned_malloc_dbg( size, align, "EXE", (uintptr_t)_ReturnAddress() );
	}

	static void AlignedFree(void* ptr)
	{
		_aligned_free_dbg(ptr);
	}
};

void InstallMemValidator()
{
	using namespace Memory;

	// TEST: Validate memory
	InjectHook( AddressByRegion_10(0x824257), malloc_validator, HookType::Jump );
	InjectHook( AddressByRegion_10(0x824269), realloc_validator, HookType::Jump );
	InjectHook( AddressByRegion_10(0x824416), calloc_validator, HookType::Jump );
	InjectHook( AddressByRegion_10(0x82413F), free_validator, HookType::Jump );
	InjectHook( AddressByRegion_10(0x828C4A), _msize_validator, HookType::Jump );

	InjectHook( AddressByRegion_10(0x82119A), _new, HookType::Jump );
	InjectHook( AddressByRegion_10(0x8214BD), _delete, HookType::Jump );

	InjectHook( AddressByRegion_10(0x72F420), &CDebugMemoryMgr::Malloc, HookType::Jump );
	InjectHook( AddressByRegion_10(0x72F430), &CDebugMemoryMgr::Free, HookType::Jump );
	InjectHook( AddressByRegion_10(0x72F440), &CDebugMemoryMgr::Realloc, HookType::Jump );
	InjectHook( AddressByRegion_10(0x72F460), &CDebugMemoryMgr::Calloc, HookType::Jump );
	InjectHook( AddressByRegion_10(0x72F4C0), &CDebugMemoryMgr::MallocAlign, HookType::Jump );
	InjectHook( AddressByRegion_10(0x72F4F0), &CDebugMemoryMgr::AlignedFree, HookType::Jump );


	PutStaticValidator( AddressByRegion_10(0xAAE950), AddressByRegion_10(0xB4C310) ); // CStore
	PutStaticValidator( AddressByRegion_10(0xA9AE00), AddressByRegion_10(0xA9AE58) ); // fx_c
}

#endif


// Hooks
__declspec(naked) void LightMaterialsFix()
{
	_asm
	{
		mov     [esi], edi
		mov		ebx, [ecx]
		lea     esi, [edx+4]
		mov		[ebx+4], esi
		mov		edi, [esi]
		mov		[ebx+8], edi
		add		esi, 4
		mov		[ebx+12], esi
		mov		edi, [esi]
		mov		[ebx+16], edi
		add		ebx, 20
		mov		[ecx], ebx
		ret
	}
}

static void* const TrailerDoubleRWheelsFix_ReturnFalse = AddressByVersion<void*>(0x4C9333);
static void* const TrailerDoubleRWheelsFix_ReturnTrue = AddressByVersion<void*>(0x4C9235);
__declspec(naked) void TrailerDoubleRWheelsFix()
{
	_asm
	{
		cmp		[edi]CVehicleModelInfo.m_nVehicleType, VEHICLE_TRAILER
		je		TrailerDoubleRWheelsFix_DoWheels
		cmp		eax, 2
		je		TrailerDoubleRWheelsFix_False
		cmp		eax, 5
		je		TrailerDoubleRWheelsFix_False

	TrailerDoubleRWheelsFix_DoWheels:
		jmp		TrailerDoubleRWheelsFix_ReturnTrue

	TrailerDoubleRWheelsFix_False:
		jmp		TrailerDoubleRWheelsFix_ReturnFalse
	}
}

__declspec(naked) void TrailerDoubleRWheelsFix2()
{
	_asm
	{
		add     esp, 0x18
		mov     eax, [ebx]
		mov     eax, [esi+eax+4]
		jmp		TrailerDoubleRWheelsFix
	}
}

// ============= All the legacy User Tracks hooks (ew) =============
namespace UserTracksChanges
{
	class CAEUserRadioTrackManager {};
	static ExternalMethod AEUserRadioTrackManager_Initialise(AddressByVersion<void(__thiscall*)(CAEUserRadioTrackManager*)>(0x4F35B0));
	static ExternalRef pUserTracksStuff(AddressByVersion<CAEUserRadioTrackManager**>(0x4D9B7B));

	static bool HasGameBindings()
	{
		return EnsureBindings(GTAdelete, AEUserRadioTrackManager_Initialise, pUserTracksStuff);
	}

	static struct
	{
		char			Extension[8];
		unsigned int	Codec;
	} UserTrackExtensions[] = { { ".ogg", DECODER_VORBIS }, { ".mp3", DECODER_QUICKTIME },
		{ ".wav", DECODER_WAVE }, { ".wma", DECODER_WINDOWSMEDIA },
		{ ".wmv", DECODER_WINDOWSMEDIA }, { ".aac", DECODER_QUICKTIME },
		{ ".m4a", DECODER_QUICKTIME }, { ".mov", DECODER_QUICKTIME },
		{ ".fla", DECODER_FLAC }, { ".flac", DECODER_FLAC } };

	static void (__thiscall* SetVolume)(void*,float);

	static CAEMiniaudioDecoder* __stdcall DecoderCtor(CAEDataStream* pData)
	{
		return new CAEMiniaudioDecoder(pData);
	}

	static CAEWaveDecoder* __stdcall CAEWaveDecoderInit(CAEDataStream* pStream)
	{
		return new CAEWaveDecoder(pStream);
	}

	__declspec(naked) void UserTracksFix()
	{
		_asm
		{
			push	[esp+4]
			call	SetVolume
			mov		ecx, [pUserTracksStuff]
			mov		ecx, [ecx]
			mov		byte ptr [ecx+0xD], 1
			call	AEUserRadioTrackManager_Initialise
			ret		4
		}
	}

	static void*	LoadFLAC_JumpBack = AddressByVersion<void*>(0x4F3743);
	__declspec(naked) void LoadFLAC()
	{
		_asm
		{
			jz		LoadFLAC_WindowsMedia
			sub		ebp, 2
			jnz		LoadFLAC_Return
			push	esi
			call	DecoderCtor
			jmp		LoadFLAC_Success

		LoadFLAC_WindowsMedia:
			jmp		LoadFLAC_JumpBack

		LoadFLAC_Success:
			test	eax, eax
			mov		[esp+0x20+4], eax
			jnz		LoadFLAC_Return_NoDelete

		LoadFLAC_Return:
			mov		ecx, esi
			call	CAEDataStream::~CAEDataStream
			push	esi
			call	GTAdelete
			add     esp, 4

		LoadFLAC_Return_NoDelete:
			mov     eax, [esp+0x20+4]
			mov		ecx, [esp+0x20-0xC]
			pop		esi
			pop		ebp
			pop		edi
			pop		ebx
			mov		fs:0, ecx
			add		esp, 0x10
			ret		4
		}
	}

	__declspec(naked) void FLACInit()
	{
		_asm
		{
			mov		byte ptr [ecx+0xD], 1
			jmp		AEUserRadioTrackManager_Initialise
		}
	}
}


// 1.0 ONLY BEGINS HERE
static bool			bDarkVehicleThing;
static RpLight**	pDirect;

static void* DarkVehiclesFix1_JumpBack;
 __declspec(naked) void DarkVehiclesFix1()
{
	_asm
	{
		shr     eax, 0xE
		test	al, 1
		jz		DarkVehiclesFix1_DontApply
		mov		ecx, pDirect
		mov		ecx, [ecx]
		mov		al, [ecx+2]
		test	al, 1
		jnz		DarkVehiclesFix1_DontApply
		mov		bDarkVehicleThing, 1
		jmp		DarkVehiclesFix1_Return

	DarkVehiclesFix1_DontApply:
		mov		bDarkVehicleThing, 0

	DarkVehiclesFix1_Return:
		jmp		DarkVehiclesFix1_JumpBack
	}
}

__declspec(naked) void DarkVehiclesFix2()
{
	_asm
	{
		jz		DarkVehiclesFix2_MakeItDark
		mov		al, bDarkVehicleThing
		test	al, al
		jnz		DarkVehiclesFix2_MakeItDark
		mov		eax, 0x5D9A7A
		jmp		eax

	DarkVehiclesFix2_MakeItDark:
		mov		eax, 0x5D9B09
		jmp		eax
	}
}

__declspec(naked) void DarkVehiclesFix3()
{
	_asm
	{
		jz		DarkVehiclesFix3_MakeItDark
		mov		al, bDarkVehicleThing
		test	al, al
		jnz		DarkVehiclesFix3_MakeItDark
		mov		eax, 0x5D9B4A
		jmp		eax

	DarkVehiclesFix3_MakeItDark:
		mov		eax, 0x5D9CAC
		jmp		eax
	}
}

__declspec(naked) void DarkVehiclesFix4()
{
	_asm
	{
		jz		DarkVehiclesFix4_MakeItDark
		mov		al, bDarkVehicleThing
		test	al, al
		jnz		DarkVehiclesFix4_MakeItDark
		mov		eax, 0x5D9CB8
		jmp		eax

	DarkVehiclesFix4_MakeItDark:
		mov		eax, 0x5D9E0D
		jmp		eax
	}
}
// 1.0 ONLY ENDS HERE

__declspec(safebuffers) static int _Timers_ftol_internal( double timer, double& remainder )
{
	double integral;
	remainder = modf( timer + remainder, &integral );
	return int(integral);
}

int __stdcall Timers_ftol_PauseMode( double timer )
{
	static double TimersRemainder = 0.0;
	return _Timers_ftol_internal( timer, TimersRemainder );
}

int __stdcall Timers_ftol_NonClipped( double timer )
{
	static double TimersRemainder = 0.0;
	return _Timers_ftol_internal( timer, TimersRemainder );
}

int __stdcall Timers_ftol( double timer )
{
	static double TimersRemainder = 0.0;
	return _Timers_ftol_internal( timer, TimersRemainder );
}

int __stdcall Timers_ftol_SCMdelta( double timer )
{
	static double TimersRemainder = 0.0;
	return _Timers_ftol_internal( timer, TimersRemainder );
}

__declspec(naked) void asmTimers_ftol_PauseMode()
{
	_asm
	{
		sub		esp, 8
		fstp	qword ptr [esp]
		call	Timers_ftol_PauseMode
		ret
	}
}

__declspec(naked) void asmTimers_ftol_NonClipped()
{
	_asm
	{
		sub		esp, 8
		fstp	qword ptr [esp]
		call	Timers_ftol_NonClipped
		ret
	}
}

__declspec(naked) void asmTimers_ftol()
{
	_asm
	{
		sub		esp, 8
		fstp	qword ptr [esp]
		call	Timers_ftol
		ret
	}
}

__declspec(naked) void asmTimers_SCMdelta()
{
	_asm
	{
		sub		esp, 8
		fstp	qword ptr [esp]
		call	Timers_ftol_SCMdelta
		ret
	}
}

__declspec(naked) void FixedCarDamage()
{
	_asm
	{
		fldz
		fcomp	dword ptr [esp+0x20+0x10]
		fnstsw  ax
		test    ah, 5
		jp		FixedCarDamage_Negative
		movzx   eax, byte ptr [edi+0x21]
		ret

	FixedCarDamage_Negative:
		movzx   eax, byte ptr [edi+0x24]
		ret
	}
}

__declspec(naked) void CdStreamThreadHighSize()
{
	_asm
	{
		xor		edx, edx
		shld	edx, ecx, 11
		shl		ecx, 11
		mov		[esi]CdStream.overlapped.Offset, ecx // OVERLAPPED.Offset
		mov		[esi]CdStream.overlapped.OffsetHigh, edx // OVERLAPPED.OffsetHigh

		mov		edx, [esi]CdStream.nSectorsToRead
		ret
	}
}

__declspec(naked) void WeaponRangeMult_VehicleCheck()
{
	_asm
	{
		mov		eax, [edx]CPed.m_nPedFlags
		test    ah, 1
		jz		WeaponRangeMult_VehicleCheck_NotInCar
		mov		eax, [edx]CPed.m_pMyVehicle
		ret

	WeaponRangeMult_VehicleCheck_NotInCar:
		xor		eax, eax
		ret
	}
}


// ============= Small Steam texts =============
namespace SmallSteamTexts
{
	static const float		fSteamSubtitleSizeX = 0.45f;
	static const float		fSteamSubtitleSizeY = 0.9f;
	static const float		fSteamRadioNamePosY = 33.0f;
	static const float		fSteamRadioNameSizeX = 0.4f;
	static const float		fSteamRadioNameSizeY = 0.6f;

	static const double		dRetailSubtitleSizeX = 0.58;
	static const double		dRetailSubtitleSizeY = 1.2;
	static const double		dRetailSubtitleSizeY2 = 1.22;
	static const double		dRetailRadioNamePosY = 22.0;
	static const double		dRetailRadioNameSizeX = 0.6;
	static const double		dRetailRadioNameSizeY = 0.9;
}

#pragma comment(lib, "shlwapi.lib")

static BOOL (*IsAlreadyRunning)();
BOOL InjectDelayedPatches_10()
{
	if ( !IsAlreadyRunning() )
	{
		using namespace Memory;

		const HINSTANCE hInstance = GetModuleHandle(nullptr);
		auto Protect = ScopedUnprotect::SectionOrFullModule(hInstance, ".text");
		auto Protect2 = ScopedUnprotect::Section(hInstance, ".rdata");

		// Obtain a path to the ASI
		wchar_t			wcModulePath[MAX_PATH];
		GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), wcModulePath, _countof(wcModulePath) - 3); // Minus max required space for extension
		PathRenameExtensionW(wcModulePath, L".ini");

		const ModuleList moduleList;

		const bool		bHasImVehFt = moduleList.Get(L"ImVehFt") != nullptr;
		const bool		bSAMP = moduleList.Get(L"samp") != nullptr;
		const bool		bSARender = moduleList.Get(L"SARender") != nullptr;
		const bool		bOutfit = moduleList.Get(L"outfit") != nullptr;
		const bool		bSAMPGraphicsRestore = moduleList.Get(L"SAMPGraphicRestore") != nullptr;

		if ( bSAMP )
		{
			LSRPMode::ReadServersList(wcModulePath);
			LSRPMode::DetectPlayingOnLSRP();
		}

		const HMODULE skygfxModule = moduleList.Get( L"skygfx" );
		const HMODULE modloaderModule = moduleList.Get( L"modloader" );

		ReadRotorFixExceptions(wcModulePath);
		ReadLightbeamFixExceptions(wcModulePath);
		const bool bHookDoubleRwheels = ReadDoubleRearWheels(wcModulePath);

		const bool bHasDebugMenu = DebugMenuLoad();

		const std::initializer_list<uint8_t> fadd = { 0xD8, 0x05 };
		const std::initializer_list<uint8_t> fsub = { 0xD8, 0x25 };
		const std::initializer_list<uint8_t> fld = { 0xD9, 0x05 };

#ifdef _DEBUG
		if ( bHasDebugMenu )
		{
			DebugMenuAddVar( "SilentPatch", "Force LS-RP Mode", &LSRPMode::ModeForced, nullptr );
		}
#endif

		if (const int INIoption = GetPrivateProfileIntW(L"SilentPatch", L"SunSizeHack", -1, wcModulePath); INIoption != -1 && !bSAMP)
		{
			using namespace SunSizeHack;

			bEnableHack = INIoption != 0;

			InterceptMemDisplacement(0x6FC5AA, fFarClipZ, fHackedFarClipZ);
			InterceptCall(0x53C136, orgDoSunAndMoon, DoSunAndMoon_SunSizeHack);

			if (bHasDebugMenu)
			{
				DebugMenuAddVar("SilentPatch", "Sun size hack", &bEnableHack, nullptr);
			}
		}

		if ( !bSARender )
		{
			// Alpha render states on rotors and propellers
			if (BlurredRotorsAtomicRender::HasGameBindings())
			{
				using namespace BlurredRotorsAtomicRender;

				auto PatchRenderCB = [](uintptr_t address, bool fallback, RpAtomic*(*&org)(RpAtomic*), RpAtomic*(&replaced)(RpAtomic*))
					{
						if (!fallback)
						{
							org = *((RpAtomic*(**)(RpAtomic*))address);
							Patch(address, &replaced);
						}
						else
						{
							InterceptCall(address, org, replaced);
						}
					};

				std::array<std::pair<uintptr_t, bool>, 4> heli_rotor_render = { {
					{ 0x7341D9, false },
					{ 0x73421D, true },
					{ 0x734127, false },
					{ 0x73414D, true },
				} };

				std::array<std::pair<uintptr_t, bool>, 2> plane_prop_render = { {
					{ 0x73445E, false },
					{ 0x73448C, true },
				} };

				HookEach_HeliRotor(heli_rotor_render, PatchRenderCB);
				HookEach_PlaneProp(plane_prop_render, PatchRenderCB);
			}

			// Weapons rendering
			if ( HasGameBindings_WeaponRenderingFixes() && !bOutfit )
			{
				if ( bSAMP )
				{
					CPed::orgGetWeaponSkillForRenderWeaponPedsForPC.Bind(&CPed::GetWeaponSkillForRenderWeaponPedsForPC_SAMP);
				}

				InjectHook(0x5E7859, RenderWeapon);
				InjectHook(0x732F30, RenderWeaponPedsForPC, HookType::Jump);
			}
		}

		if ( GetPrivateProfileIntW(L"SilentPatch", L"EnableScriptFixes", 0, wcModulePath) != 0 )
		{
			using namespace ScriptFixes;

			// Gym glitch fix
			if (HasGameBindings_GymGlitch())
			{
				Patch<WORD>(0x470B03, 0xCD8B);
				Patch<DWORD>(0x470B0A, 0x8B04508B);
				Patch<WORD>(0x470B0E, 0x9000);
				Nop(0x470B10, 1);
				InjectHook(0x470B05, &CRunningScript::GetDay_GymGlitch, HookType::Call);
			}

			// Basketball fix
			InterceptCall( 0x5D18F0, orgTheScriptsLoad, TheScriptsLoad_BasketballFix );

			std::array<uintptr_t, 2> wipeLocalVars = { 0x489A70, 0x4899F0 };
			HookEach_SCMFixes(wipeLocalVars, InterceptCall);
		}

		if ( GetPrivateProfileIntW(L"SilentPatch", L"SkipIntroSplashes", 0, wcModulePath) != 0 )
		{
			// Skip the damn intro splash
			Patch<WORD>(AddressByRegion_10<DWORD>(0x748AA8), 0x3DEB);
		}


		if ( const int INIoption = GetPrivateProfileIntW(L"SilentPatch", L"SmallSteamTexts", -1, wcModulePath); INIoption != -1 )
		{
			using namespace SmallSteamTexts;

			static bool bSmallSteamTexts = INIoption != 0;

			static float* orgSubtitleSizeX;
			static float* orgSubtitleSizeY;
			static float* orgRadioNamePosY;
			static float* orgRadioNameSizeX;
			static float* orgRadioNameSizeY;

			auto ToggleSteamTexts = []()
			{
				if (bSmallSteamTexts)
				{
					Memory::VP::Patch<const void*>(0x58C387, &fSteamSubtitleSizeY);
					Memory::VP::Patch<const void*>(0x58C40F, &fSteamSubtitleSizeY);
					Memory::VP::Patch<const void*>(0x58C4CE, &fSteamSubtitleSizeY);

					Memory::VP::Patch<const void*>(0x58C39D, &fSteamSubtitleSizeX);
					Memory::VP::Patch<const void*>(0x58C425, &fSteamSubtitleSizeX);
					Memory::VP::Patch<const void*>(0x58C4E4, &fSteamSubtitleSizeX);

					Memory::VP::Patch<const void*>(0x4E9FD8, &fSteamRadioNamePosY);
					Memory::VP::Patch<const void*>(0x4E9F22, &fSteamRadioNameSizeY);
					Memory::VP::Patch<const void*>(0x4E9F38, &fSteamRadioNameSizeX);
				}
				else
				{
					assert( orgSubtitleSizeY != nullptr && orgSubtitleSizeX != nullptr && orgRadioNamePosY != nullptr && orgRadioNameSizeY != nullptr && orgRadioNameSizeX != nullptr );

					Memory::VP::Patch<const void*>(0x58C387, orgSubtitleSizeY);
					Memory::VP::Patch<const void*>(0x58C40F, orgSubtitleSizeY);
					Memory::VP::Patch<const void*>(0x58C4CE, orgSubtitleSizeY);

					Memory::VP::Patch<const void*>(0x58C39D, orgSubtitleSizeX);
					Memory::VP::Patch<const void*>(0x58C425, orgSubtitleSizeX);
					Memory::VP::Patch<const void*>(0x58C4E4, orgSubtitleSizeX);

					Memory::VP::Patch<const void*>(0x4E9FD8, orgRadioNamePosY);
					Memory::VP::Patch<const void*>(0x4E9F22, orgRadioNameSizeY);
					Memory::VP::Patch<const void*>(0x4E9F38, orgRadioNameSizeX);
				}
			};

			if (bSmallSteamTexts)
			{
				// We're on 1.0 - make texts smaller
				ToggleSteamTexts();
			}

			if (bHasDebugMenu)
			{
				orgSubtitleSizeX = *(float**)0x58C39D;
				orgSubtitleSizeY = *(float**)0x58C387;
				orgRadioNamePosY = *(float**)0x4E9FD8;
				orgRadioNameSizeY = *(float**)0x4E9F22;
				orgRadioNameSizeX = *(float**)0x4E9F38;

				DebugMenuAddVar("SilentPatch", "Small Steam texts", &bSmallSteamTexts, ToggleSteamTexts);
			}
		}


		if ( const int INIoption = GetPrivateProfileIntW(L"SilentPatch", L"ColouredZoneNames", -1, wcModulePath); INIoption != -1 && ColouredZoneNames::HasGameBindings() )
		{
			using namespace ColouredZoneNames;

			// Coloured zone names
			bColouredZoneNames = INIoption != 0;

			Patch<WORD>(0x58ADBE, 0x0E75);
			Patch<WORD>(0x58ADC5, 0x0775);

			InjectHook(0x58ADE4, &BlendGangColour_Dynamic);

			if ( bHasDebugMenu )
			{
				DebugMenuAddVar( "SilentPatch", "Coloured zone names", &bColouredZoneNames, nullptr );
			}
		}

		// Apply the correct colors on detached car components
		bool bObjectRenderPatched = false;
		if (HasGameBindings_DetachedPartRenderingFix())
		{
			InterceptCall(0x59F1ED, CObject::orgRender_DetachedPartRenderingFix, &CObject::Render_DetachedPartRenderingFix);
			bObjectRenderPatched = true;
		}

		// ImVehFt conflicts
		if ( !bHasImVehFt )
		{
			// Cars getting dirty
			if (HasGameBindings_DirtRemapFix())
			{
				InjectHook( 0x5D5DB0, RemapDirt, HookType::Jump );
				InjectHook(0x4C9648, &CVehicleModelInfo::FindEditableMaterialList, HookType::Call);
				Patch<DWORD>(0x4C964D, 0x0FEBCE8B);

				DWORD*		pVMT = *(DWORD**)0x4C75FC;
				if (ModCompat::Utils::GetModuleHandleFromAddress(pVMT) == hInstance)
				{
					Read(&pVMT[7], CVehicleModelInfo::orgShutdown_CarDirtFix);
					Patch(&pVMT[7], &CVehicleModelInfo::Shutdown_CarDirtFix);
				}
			}
		}

		// Enable directional lights on flying car components
		// This fix is technically separate from the component fix, but with SkyGfx, if the above fix fails to apply, detached parts will be green once they're detached,
		// as SkyGfx lacks a fallback "fixing up" those colors to black, unlike the PC code.
		// We then need to disable this fix, or else people think it's a regression caused by SP.
		if (bObjectRenderPatched)
		{
			using namespace LitFlyingComponents;

			InterceptCall(0x6A8BBE, orgWorldAdd, WorldAdd_SetLightObjectFlag);
		}

		if ( !bHasImVehFt && !bSAMP && HasGameBindings_CustomCarPlateFix() )
		{
			// Properly random numberplates
			InjectHook(0x4C9660, &CVehicleModelInfo::SetCarCustomPlate);
			InjectHook(0x6D6A58, &CVehicle::CustomCarPlate_TextureCreate);
			InjectHook(0x6D651C, &CVehicle::CustomCarPlate_BeforeRenderingStart);
			InjectHook(0x6FDFE0, CCustomCarPlateMgr::SetupClumpAfterVehicleUpgrade, HookType::Jump);
			InjectHook(0x6D0E53, &CVehicle::CustomCarPlate_AfterRenderingStop);
			Nop(0x6D6517, 2);
			Nop(0x6D0E43, 2);
		}

		// SSE conflicts
		if ( moduleList.Get(L"shadows") == nullptr )
		{
			if (HasGameBindings_ShadowRenderingFixes())
			{
				Patch<DWORD>(0x70665C, 0x52909090);
				InjectHook(0x706662, &CShadowCamera::Update);
			}

			// Disable alpha test for stored shadows
			{
				using namespace StaticShadowAlphaFix;

				ReadCall( 0x53E0C8, orgRenderStoredShadows );
				InjectHook( 0x53E0C8, RenderStoredShadows_StateFix );
			}
		}

		// Bigger streamed entity linked lists
		// Increase only if they're not increased already
		if ( *(DWORD*)0x5B8E55 == 12000 )
		{
			Patch<DWORD>(0x5B8E55, 15000);
			Patch<DWORD>(0x5B8EB0, 15000);
		}

		// Read CCustomCarPlateMgr::GeneratePlateText from here
		// to work fine with Deji's Custom Plate Format
		ReadCall( 0x4C9484, *CCustomCarPlateMgr::GeneratePlateText.Put() );


		if ( bHookDoubleRwheels && ms_modelInfoPtrs.Ensure() )
		{
			// Double rwheels whitelist
			// push ecx
			// push edi
			// call CheckDoubleRWheelsWhitelist
			// test al, al
			Patch<uint16_t>( 0x4C9239, 0x5751 );
			InjectHook( 0x4C9239+2, CheckDoubleRWheelsList, HookType::Call );
			Patch<uint16_t>( 0x4C9239+7, 0xC084 );
			Nop( 0x4C9239+9, 1 );
		}

		if ( *(DWORD*)0x4065BB == 0x3B0BE1C1 )
		{
			// Handle IMGs bigger than 4GB
			Nop( 0x4065BB, 3 );
			Nop( 0x4065C2, 1 );
			InjectHook( 0x4065C2+1, CdStreamThreadHighSize, HookType::Call );
			Patch<const void*>( 0x406620+2, &pCdStreamSetFilePointer );
		}


		// Fix directional light position
		if ( const int INIoption = GetPrivateProfileIntW(L"SilentPatch", L"DirectionalFromSun", -1, wcModulePath); INIoption != -1 && DirectionalFromSun::HasGameBindings() )
		{
			using namespace DirectionalFromSun;

			bUseAaronSun = INIoption != 0;

			ReadCall( 0x53E997, orgSetLightsWithTimeOfDayColour );
			InjectHook( 0x53E997, SetLightsWithTimeOfDayColour_SilentPatch );
			Patch<const void*>( 0x735618 + 2, &curVecToSun.x );
			Patch<const void*>( 0x73561E + 2, &curVecToSun.y );
			Patch<const void*>( 0x735624 + 1, &curVecToSun.z );

			if ( bHasDebugMenu )
			{
				DebugMenuAddVar( "SilentPatch", "Directional from sun", &bUseAaronSun, nullptr );

	#ifndef NDEBUG
				// Switch for fixed PC vehicle lighting
				static bool bFixedPCVehLight = true;
				DebugMenuAddVar( "SilentPatch", "Fixed PC vehicle light", &bFixedPCVehLight, []() {
					if ( bFixedPCVehLight )
					{
						Memory::VP::Patch<float>(0x5D88D1 + 6, 0);
						Memory::VP::Patch<float>(0x5D88DB + 6, 0);
						Memory::VP::Patch<float>(0x5D88E5 + 6, 0);

						Memory::VP::Patch<float>(0x5D88F9 + 6, 0);
						Memory::VP::Patch<float>(0x5D8903 + 6, 0);
						Memory::VP::Patch<float>(0x5D890D + 6, 0);
					}
					else
					{
						Memory::VP::Patch<float>(0x5D88D1 + 6, 0.25f);
						Memory::VP::Patch<float>(0x5D88DB + 6, 0.25f);
						Memory::VP::Patch<float>(0x5D88E5 + 6, 0.25f);

						Memory::VP::Patch<float>(0x5D88F9 + 6, 0.75f);
						Memory::VP::Patch<float>(0x5D8903 + 6, 0.75f);
						Memory::VP::Patch<float>(0x5D890D + 6, 0.75f);
					}

					auto [start, end] = std::minmax({ 0x5D88D1 + 6, 0x5D88DB + 6, 0x5D88E5 + 6, 0x5D88F9 + 6, 0x5D8903 + 6, 0x5D890D + 6});
					Memory::FlushCodeChanges(start, end - start + sizeof(float));
				} );
	#endif
			}
		}

		// Minimal HUD
		if ( const int INIoption = GetPrivateProfileIntW(L"SilentPatch", L"MinimalHUD", -1, wcModulePath); INIoption != -1 )
		{
			using namespace MinimalHUD;

			// Fix original bugs
			Patch( 0x58950E, { 0x90, 0xFF, 0x74, 0x24, 0x1C } );
			InjectHook( 0x58951D, &SetRGBA_FloatAlpha );

			Patch( 0x58D88A, { 0x90, 0xFF, 0x74, 0x24, 0x20 + 0x10 } );
			ReadCall( 0x58D8FD, orgRenderOneXLUSprite );
			InjectHook( 0x58D8FD, &RenderXLUSprite_FloatAlpha );

			// Re-enable
			if ( INIoption != 0 )
			{
				Patch<int32_t>( 0x588905 + 1, 0 );
			}

			if ( bHasDebugMenu )
			{
				static bool bMinimalHUDEnabled = INIoption != 0;
				DebugMenuAddVar( "SilentPatch", "Minimal HUD", &bMinimalHUDEnabled, []() {
					if ( bMinimalHUDEnabled )
					{
						Memory::VP::Patch<int32_t>( 0x588905 + 1, 0 );
					}
					else
					{
						Memory::VP::Patch<int32_t>( 0x588905 + 1, 5 );
					}
					Memory::FlushCodeChanges(0x588905 + 1, sizeof(int32_t));

					// Call CHud::ReInitialise
					auto ReInitialise = (void(*)())0x588880;
					ReInitialise();
				} );
			}
		}

		// True invincibility - not being hurt by Police Maverick bullets anymore
		if ( !bSAMP )
		{
			int INIoption = GetPrivateProfileIntW(L"SilentPatch", L"TrueInvincibility", -1, wcModulePath);
			if (INIoption == -1)
			{
				// Minor spelling mistake, keep it for backwards compatibility with old INI files
				INIoption = GetPrivateProfileIntW(L"SilentPatch", L"TrueInvicibility", -1, wcModulePath);
			}
			if (INIoption != -1)
			{
				using namespace TrueInvincibility;

				isEnabled = INIoption != 0;
				WillKillJumpBack = 0x4B3238;
				InjectHook( 0x4B322E, ComputeWillKillPedHook, HookType::Jump );

				if ( bHasDebugMenu )
				{
					DebugMenuAddVar( "SilentPatch", "True invincibility", &isEnabled, nullptr );
				}
			}
		}

		// Moonphases
		// Not taking effect with new skygfx since aap has it too now
		if ( !bSAMP && MoonphasesFix::HasGameBindings() && !ModCompat::SkyGfx::PatchesMoonphases( skygfxModule ) )
		{
			using namespace MoonphasesFix;

			ReadCall( 0x713B74, orgRenderOneXLUSprite );
			InjectHook( 0x713C4C, RenderOneXLUSprite_MoonPhases );
		}

		FLAUtils::Init( moduleList );

		// Race condition in CdStream fixed
		// Not taking effect with modloader
		if ( !ModCompat::ModloaderCdStreamRaceConditionAware( modloaderModule ) )
		{
			// Don't patch if old FLA and enhanced IMGs are in place
			// For new FLA, we patch everything except CdStreamThread and then interop with FLA
			const bool flaBugAware = FLAUtils::CdStreamRaceConditionAware();
			const bool usesEnhancedImages = FLAUtils::UsesEnhancedIMGs();

			if ( !usesEnhancedImages || flaBugAware )
			{
				ReadCall( 0x406C78, CdStreamSync::orgCdStreamInitThread );
				InjectHook( 0x406C78, CdStreamSync::CdStreamInitThread );

				{
					const uintptr_t address = ModCompat::Utils::GetFunctionAddrIfRerouted(0x406460);

					const uintptr_t waitForSingleObject = address + 0x1D;
					const uint8_t orgCode[] = { 0x8B, 0x46, 0x04, 0x85, 0xC0, 0x74, 0x10, 0xC6, 0x46, 0x0D, 0x01 };
					if ( memcmp( orgCode, (void*)waitForSingleObject, sizeof(orgCode) ) == 0 )
					{
						VP::Patch( waitForSingleObject, { 0x56, 0xFF, 0x15 } );
						VP::Patch( waitForSingleObject + 3, &CdStreamSync::CdStreamSyncOnObject );
						VP::Patch( waitForSingleObject + 3 + 4, { 0x5E, 0xC3 } );

						{
							const uint8_t orgCode1[] = { 0xFF, 0x15 };
							const uint8_t orgCode2[] = { 0x48, 0xF7, 0xD8 };
							const uintptr_t getOverlappedResult = address + 0x5F;
							if ( memcmp( orgCode1, (void*)getOverlappedResult, sizeof(orgCode1) ) == 0 &&
								memcmp( orgCode2, (void*)(getOverlappedResult + 6), sizeof(orgCode2) ) == 0 )
							{
								VP::Patch( getOverlappedResult + 2, &CdStreamSync::pGetOverlappedResult );
								VP::Patch( getOverlappedResult + 6, { 0x5E, 0xC3 } ); // pop esi / retn
							}
						}
					}
				}

				if ( !usesEnhancedImages )
				{
					Patch( 0x406669, { 0x56, 0xFF, 0x15 } );
					Patch( 0x406669 + 3, &CdStreamSync::CdStreamThreadOnObject );
					Patch( 0x406669 + 3 + 4, { 0xEB, 0x0F } );
				}

				Patch( 0x406910, { 0xFF, 0x15 } );
				Patch( 0x406910 + 2, &CdStreamSync::CdStreamInitializeSyncObject );
				Nop( 0x406910 + 6, 4 );
				Nop( 0x406910 + 0x16, 2 );

				Patch( 0x4063B5, { 0x56, 0x50 } );
				InjectHook( 0x4063B5 + 2, CdStreamSync::CdStreamShutdownSyncObject_Stub, HookType::Call );
			}
		}

		// For imfast compatibility
		if ( MemEquals( 0x590ADE, { 0xFF, 0x05 } ) && LoopDisplayedSplashes::HasGameBindings() )
		{
			using namespace LoopDisplayedSplashes;

			// Modulo over CLoadingScreen::m_currDisplayedSplash
			Nop( 0x590ADE, 1 );
			InjectHook( 0x590ADE + 1, DoPCScreenChange_Mod, HookType::Call );
			Patch<const void*>( 0x590042 + 2, &currDisplayedSplash_ForLastSplash );
		}

		// Lightbeam fix debug menu
		if ( bHasDebugMenu )
		{
			static const char * const str[] = { "Off", "Default", "On" };

			DebugMenuEntry *e = DebugMenuAddVar( "SilentPatch", "Rotors fix", &CVehicle::ms_rotorFixOverride, nullptr, 1, -1, 1, str);
			DebugMenuEntrySetWrap(e, true);

			if ( LightbeamFix::hookedSuccessfully )
			{
				e = DebugMenuAddVar( "SilentPatch", "Lightbeam fix", &CVehicle::ms_lightbeamFixOverride, nullptr, 1, -1, 1, str);
				DebugMenuEntrySetWrap(e, true);
			}
		}

		// Locale based metric/imperial system INI/debug menu
		{
			using namespace Localization;

			forcedUnits = static_cast<int8_t>(GetPrivateProfileIntW(L"SilentPatch", L"Units", -1, wcModulePath));
			if ( bHasDebugMenu )
			{
				static const char * const str[] = { "Default", "Metric", "Imperial" };
				DebugMenuEntry *e = DebugMenuAddVar( "SilentPatch", "Forced units", &forcedUnits, nullptr, 1, -1, 1, str );
				DebugMenuEntrySetWrap(e, true);
			}
		}

		// Register CBaseModelInfo::GetModelInfo for SVF so we can resolve model names
		{
			using namespace ModelIndicesReadyHook;

			auto func = (void*(*)(const char*, int*))0x4C5940;

			InterceptCall(0x5B922F, orgMatchAllModelStrings, MatchAllModelStrings_ReadySVF);
			SVF::RegisterGetModelInfoCB(func);
		}


		// Disable building pipeline for skinned objects (like parachute)
		// SAMP Graphics Restore fixes the bug preventing this fix from working right
		if (!bSAMP || bSAMPGraphicsRestore)
		{
			using namespace SkinBuildingPipelineFix;

			InterceptCall(0x5D7F1E, orgCustomBuildingDNPipeline_CustomPipeAtomicSetup, CustomBuildingDNPipeline_CustomPipeAtomicSetup_Skinned);
		}


		// Fix some big messages staying on screen longer at high resolutions due to a cut sliding text feature
		// Also since we're touching it, optionally allow to re-enable this feature.
		if (const int INIoption = GetPrivateProfileIntW(L"SilentPatch", L"SlidingMissionTitleText", -1, wcModulePath); INIoption != -1 && SlidingTextsScalingFixes::HasGameBindings())
		{
			using namespace SlidingTextsScalingFixes;

			pBigMessageX = *(std::array<float, 6>**)(0x58C919 + 2);

			std::array<uintptr_t, 1> slidingMessage1 = { 0x58D470 };

			std::array<uintptr_t, 1> textWrapFix = { 0x58D2A9 };

			BigMessageSlider<1>::bSlidingEnabled = INIoption != 0;
			BigMessageSlider<1>::HookEach_PrintString(slidingMessage1, InterceptCall);
			BigMessageSlider<1>::HookEach_RightJustifyWrap(textWrapFix, InterceptCall);

			if (bHasDebugMenu)
			{
				DebugMenuAddVar("SilentPatch", "Sliding mission title text", &BigMessageSlider<1>::bSlidingEnabled, nullptr);
			}
		}

		if (const int INIoption = GetPrivateProfileIntW(L"SilentPatch", L"SlidingOddJobText", -1, wcModulePath); INIoption != -1 && SlidingTextsScalingFixes::HasGameBindings())
		{
			using namespace SlidingTextsScalingFixes;

			pOddJob2XOffset = *(float**)(0x58D077 + 2);

			std::array<uintptr_t, 1> slidingOddJob2 = { 0x58D21A };

			OddJob2Slider::bSlidingEnabled = INIoption != 0;
			OddJob2Slider::HookEach_PrintString(slidingOddJob2, InterceptCall);

			if (bHasDebugMenu)
			{
				DebugMenuAddVar("SilentPatch", "Sliding odd job text", &OddJob2Slider::bSlidingEnabled, nullptr);
			}
		}


		// Fix Map screen boundaries and the cursor not scaling to resolution
		// Debugged by Wesser
		// Moved here for compatibility with wshps.asi
		if (MapScreenScalingFixes::HasGameBindings())
		{
			using namespace MapScreenScalingFixes;

			// Even though those two patch the same function, treating them as separate patches makes retaining compatibility
			// with the widescreen fix easy.
			if (MemEquals(0x588251, fld) && MemEquals(0x588265, fadd) && MemEquals(0x5882A8, fld) && MemEquals(0x5882C6, fadd))
			{
				std::array<float**, 2> cursorXSizes = { (float**)(0x588251 + 2), (float**)(0x588265 + 2) };
				std::array<float**, 2> cursorYSizes = { (float**)(0x5882A8 + 2), (float**)(0x5882C6 + 2) };

				HookEach_CursorXSize(cursorXSizes, InterceptMemDisplacement);
				HookEach_CursorYSize(cursorYSizes, InterceptMemDisplacement);
				InterceptCall(0x58822D, orgLimitToMap_RecalculateSizes, LimitToMap_RecalculateSizes<cursorXSizes.size(), cursorYSizes.size()>);
			}

			// Only patch this function if wshps.asi hasn't changed the way it's being called
			// The expected code:
			// lea edx, [esp+70h+a1.y]
			// push edx
			// lea eax, [esp+74h+a1]
			// push eax
			if (MemEquals(0x588223, {0x8D, 0x54, 0x24, 0x4C, 0x52, 0x8D, 0x44, 0x24, 0x4C, 0x50 }))
			{
				InterceptCall(0x58822D, orgLimitToMap_Scale, LimitToMap_Scale);
			}
		}


		// Fix text background padding not scaling to resolution
		// Debugged by Wesser
		// Moved here for compatibility with wshps.asi
		if (!bSAMP && TextRectPaddingScalingFixes::HasGameBindings())
		{
			using namespace TextRectPaddingScalingFixes;

			// Verify that all fadd and fsub instructions are intact
			// Patterns would do it for us for free, but 1.0 does not use them...
			if (MemEquals(0x71A653, fadd) && MemEquals(0x71A66B, fadd) && MemEquals(0x71A69D, fsub) && MemEquals(0x71A6AB, fadd) &&
				MemEquals(0x71A6BF, fsub) && MemEquals(0x71A6EC, fadd))
			{
				std::array<float**, 4> paddingXSizes = {
					(float**)(0x71A653 + 2), (float**)(0x71A66B + 2),
					(float**)(0x71A69D + 2), (float**)(0x71A6AB + 2),
				};
				std::array<float**, 2> paddingYSizes = {
					(float**)(0x71A6BF + 2), (float**)(0x71A6EC + 2),
				};

				HookEach_PaddingXSize(paddingXSizes, InterceptMemDisplacement);
				HookEach_PaddingYSize(paddingYSizes, InterceptMemDisplacement);
				InterceptCall(0x71A631, orgProcessCurrentString, ProcessCurrentString_Scale<paddingXSizes.size(), paddingYSizes.size()>);
			}
		}


		// Fix credits not scaling to resolution
		// Moved here for compatibility with wshps.asi
		if (CreditsScalingFixes::HasGameBindings() && MemEquals(0x5A8679, {0xD8, 0xC1, 0xD8, 0x05}) && MemEquals(0x5A8679+8, {0xD8, 0x64, 0x24, 0x18, 0xD9, 0x54, 0x24, 0x14})) // Verify wshps.asi isn't already patching the credits
		{
			using namespace CreditsScalingFixes;

			std::array<uintptr_t, 2> creditPrintString = { 0x5A8707, 0x5A8785 };

			HookEach_PrintString(creditPrintString, InterceptCall);
			InterceptCall(0x5A86C0, orgSetScale, SetScale_ScaleToRes);

			// Fix the credits cutting off on the bottom early, they don't do that in III
			// but it regressed in VC and SA
			static const float topMargin = 1.0f;
			static const float bottomMargin = -(**(float**)(0x5A869A + 2));

			Patch(0x5A8689 + 2, &topMargin);
			Patch(0x5A869A + 2, &bottomMargin);

			// As we now scale everything on PrintString time, the resolution height checks need to be unscaled.
			Patch(0x5A8660 + 2, &FIXED_RES_HEIGHT_SCALE);
			Patch(0x5AF8C9 + 2, &FIXED_RES_HEIGHT_SCALE);
		}

		// Speech system fixes (continued)
		{
			using namespace SpeechSystemFixes;

			auto gSpeechContextLookup = *reinterpret_cast<int16_t (**)[8]>(0x4E4492 + 1);
			auto gGenSpeechLookup = *reinterpret_cast<uint8_t (**)[209][2]>(0x4E5A0C + 4);
			auto gEmgSpeechLookup = *reinterpret_cast<int16_t (**)[46][2]>(0x4E5A4A + 3);
			auto gGfdSpeechLookup = *reinterpret_cast<int16_t (**)[18][2]>(0x4E5B51 + 3);
			auto gPlySpeechLookup = *reinterpret_cast<int16_t (**)[20][2]>(0x4E5AD4 + 3);
			auto gGngSpeechLookup = *reinterpret_cast<int16_t (**)[52][2]>(0x4E5B12 + 3);
			auto gSpecialPedVoiceLookup = *reinterpret_cast<int16_t (**)[3]>(0x4E41AF + 3);
			auto gSpecialPedVoiceNameLookup = *reinterpret_cast<char (**)[20]>(0x4E4186 + 3);
			const size_t numPedVoiceLookups = *reinterpret_cast<uint8_t*>(0x4E419B + 3);
			if (ModCompat::Utils::GetModuleHandleFromAddress(gSpeechContextLookup) == hInstance)
			{
				Patches::PatchGlobalSpeechContexts(gSpeechContextLookup);
			}
			if (ModCompat::Utils::GetModuleHandleFromAddress(gGenSpeechLookup) == hInstance)
			{
				Patches::PatchGenSpeechContexts(gGenSpeechLookup);
			}
			if (ModCompat::Utils::GetModuleHandleFromAddress(gEmgSpeechLookup) == hInstance)
			{
				Patches::PatchEmgSpeechContexts(gEmgSpeechLookup);
			}
			if (ModCompat::Utils::GetModuleHandleFromAddress(gGfdSpeechLookup) == hInstance)
			{
				Patches::PatchGfdSpeechContexts(gGfdSpeechLookup);
			}
			if (ModCompat::Utils::GetModuleHandleFromAddress(gPlySpeechLookup) == hInstance)
			{
				Patches::PatchPlySpeechContexts(gPlySpeechLookup);
			}
			if (ModCompat::Utils::GetModuleHandleFromAddress(gGngSpeechLookup) == hInstance)
			{
				Patches::PatchGngSpeechContexts(gGngSpeechLookup);
			}
			if (ModCompat::Utils::GetModuleHandleFromAddress(gSpecialPedVoiceLookup) == hInstance &&
				ModCompat::Utils::GetModuleHandleFromAddress(gSpecialPedVoiceNameLookup) == hInstance)
			{
				Patches::PatchPedVoiceNameLookups(gSpecialPedVoiceLookup, gSpecialPedVoiceNameLookup, numPedVoiceLookups);
			}
		}


		// Make the game use the desktop refresh rate instead of a fixed 60Hz
		if (GetPrivateProfileIntW(L"SilentPatch", L"UseDesktopRefreshRate", 0, wcModulePath) != 0)
		{
			// Only patch if the stock function still targets 60Hz. Otherwise, it probably was changed by Framerate Vigilante
			if (MemEquals(0x7460A0, { 0x83, 0xEC, 0x10 }) && MemEquals(0x74612A, { 0x83, 0xF8, 0x3C }))
			{
				// xor eax, eax \ retn
				Patch(0x7460A0, { 0x31, 0xC0, 0xC3 });
			}
		}


		// Remove the code disallowing Rhino from having functional lights (III/VC leftover)
		// Contributed by rx
		if (MemEquals(0x6A2EAB, {0x66u, 0x3Du, 0xB0u, 0x01u }) && MemEquals(0x6ABC81, {0x66u, 0x3Du, 0xB0u, 0x01u })) // Avoid potential incompatibilities with limit adjusters
		{
			Patch<int16_t>(0x6A2EAB + 2, -2);
			Patch<int16_t>(0x6ABC81 + 2, -2);
		}


		// Clip the cursor to the game window bounds
		// Moved here to get out of the way of SA-MP
		if (!bSAMP && ClipCursorToGameWindow::HasGameBindings())
		{
			using namespace ClipCursorToGameWindow;

			// Disable mouse re-centering
			Nop(0x53E9F1, 5);

			InterceptCall(0x53EC01, orgRsCameraShowRaster, RsCameraShowRaster_ProcessCursorClip);
			InterceptMemDisplacement(AddressByRegion_10(0x748452 + 2), orgWindowProc, pClipWindowProcA);
		}


#ifndef NDEBUG
		if ( const int QPCDays = GetPrivateProfileIntW(L"Debug", L"AddDaysToQPC", 0, wcModulePath); QPCDays != 0 )
		{
			using namespace FakeQPC;

			LARGE_INTEGER Freq;
			QueryPerformanceFrequency( &Freq );
			AddedTime = Freq.QuadPart * QPCDays * 60 * 24;

			Patch( 0x8580C8, &FakeQueryPerformanceCounter );
		}
#endif

		Memory::FlushCodeChanges();
		return FALSE;
	}
	return TRUE;
}

static char		aNoDesktopMode[64];


void Patch_SA_10(HINSTANCE hInstance)
{
	using namespace Memory;

#if MEM_VALIDATORS
	InstallMemValidator();
#endif

	// IsAlreadyRunning needs to be read relatively late - the later, the better
	{
		const uintptr_t pIsAlreadyRunning = AddressByRegion_10<uintptr_t>(0x74872D);
		ReadCall( pIsAlreadyRunning, IsAlreadyRunning );
		InjectHook(pIsAlreadyRunning, InjectDelayedPatches_10);
	}

	pDirect = *(RpLight***)0x5BA573;
	DarkVehiclesFix1_JumpBack = AddressByRegion_10<void*>(0x756D90);

	// (Hopefully) more precise frame limiter
	{
		uintptr_t pAddress = AddressByRegion_10<uintptr_t>(0x748D9B);
		ReadCall( pAddress, RsEventHandler );
		InjectHook(pAddress, NewFrameRender);
		InjectHook(AddressByRegion_10<uintptr_t>(0x748D1F), GetTimeSinceLastFrame);
	}

	//Patch<BYTE>(0x5D7265, 0xEB);

	// Heli rotors
	InterceptCall(0x6CAB80, CPlane::orgRender_RenderRotors, &CPlane::RenderRotors);
	InterceptCall(0x6C4523, CHeli::orgRender_RenderRotors, &CHeli::RenderRotors);

	// Boats
	/*Patch<BYTE>(0x4C79DF, 0x19);
	Patch<DWORD>(0x733A87, EXPAND_BOAT_ALPHA_ATOMIC_LISTS * sizeof(AlphaObjectInfo));
	Patch<DWORD>(0x733AD7, EXPAND_BOAT_ALPHA_ATOMIC_LISTS * sizeof(AlphaObjectInfo));*/

	// Fixed strafing? Hopefully
	/*static const float		fStrafeCheck = 0.1f;
	Patch<const void*>(0x61E0C2, &fStrafeCheck);
	Nop(0x61E0CA, 6);*/

	// RefFix
	static const float						fRefZVal = 1.0f;
	static const float* const				pRefFal = &fRefZVal;

	Patch<const void*>(0x6FB97A, &pRefFal);
	Patch<BYTE>(0x6FB9A0, 0);

	// Proper alpha handling for plane propellers
	if (BlurredRotorsAtomicRender::HasGameBindings())
	{
		using namespace BlurredRotorsAtomicRender;

		RenderVehicleHiDetailAlphaCB_BigVehicle = *(decltype(RenderVehicleHiDetailAlphaCB_BigVehicle)*)(0x4C797A + 1);

		orgSetAtomicRendererCB_BigVehicle = *(decltype(orgSetAtomicRendererCB_BigVehicle)*)(0x4C7B76 + 1);
		Patch(0x4C7B76 + 1, &SetAtomicRendererCB_PlaneOrBigVehicle);
	}

	// DOUBLE_RWHEELS
	Patch<WORD>(0x4C9290, 0xE281);
	Patch<int>(0x4C9292, ~(rwMATRIXTYPEMASK|rwMATRIXINTERNALIDENTITY));

	// A fix for DOUBLE_RWHEELS trailers
	InjectHook(0x4C9223, TrailerDoubleRWheelsFix, HookType::Jump);
	InjectHook(0x4C92F4, TrailerDoubleRWheelsFix2, HookType::Jump);

	// No framedelay
	Patch<WORD>(0x53E923, 0x43EB);
	Patch<BYTE>(0x53E99F, 0x10);
	Nop(0x53E9A5, 1);

	// Disable re-initialization of DirectInput mouse device by the game
	Patch<BYTE>(0x576CCC, 0xEB);
	Patch<BYTE>(0x576EBA, 0xEB);
	Patch<BYTE>(0x576F8A, 0xEB);

	// Make sure DirectInput mouse device is set non-exclusive (may not be needed?)
	Patch<DWORD>(AddressByRegion_10<DWORD>(0x7469A0), 0x9090C030);

	// Proper alpha handling for plane propellers
	if (BlurredRotorsAtomicRender::HasGameBindings())
	{
		using namespace BlurredRotorsAtomicRender;

		RenderHeliRotorAlphaCB = *(decltype(RenderHeliRotorAlphaCB)*)(0x4C7898 + 1);
		RenderHeliTailRotorAlphaCB = *(decltype(RenderHeliTailRotorAlphaCB)*)(0x4C78B1 + 1);

		orgSetAtomicRendererCB_RealHeli = *(decltype(orgSetAtomicRendererCB_RealHeli)*)(0x4C7B53 + 1);
		Patch(0x4C7B53 + 1, &SetAtomicRendererCB_RealHeli_StaticRotor);
	}

	// Hunter door render flag fix (interior no longer vanishing when looking at it from the right side)
	if (HunterDoorRenderFlagFix::HasGameBindings())
	{
		using namespace HunterDoorRenderFlagFix;

		InterceptCall(0x4C9638, orgPreprocessHierarchy, PreprocessHierarchy_UnmarkHunterDoor);
	}

	// Fixed blown up car rendering
	// ONLY 1.0
	InjectHook(0x5D993F, DarkVehiclesFix1);
	InjectHook(0x5D9A74, DarkVehiclesFix2, HookType::Jump);
	InjectHook(0x5D9B44, DarkVehiclesFix3, HookType::Jump);
	InjectHook(0x5D9CB2, DarkVehiclesFix4, HookType::Jump);

	// Bindable NUM5
	Nop(0x57DC55, 2);


	// TEMP
	//Patch<DWORD>(0x733B05, 40);
	//Patch<DWORD>(0x733B55, 40);
	//Patch<BYTE>(0x5B3ADD, 4);

	// Lightbeam fix
	// We need to check for presence of old lightbeam fix - first validate everything old SP did
	if (	MemEquals( 0x6A2E95, { 0xFF, 0x52, 0x20 } ) &&
			MemEquals( 0x6E0F63, { 0xA1 } ) &&
			MemEquals( 0x6E0F7C, { 0x8B, 0x15 } ) &&
			MemEquals( 0x6E0F95, { 0x8B, 0x0D } ) &&
			MemEquals( 0x6E0FAF, { 0xA1 } ) &&
			MemEquals( 0x6E13D5, { 0xA1 } ) &&
			MemEquals( 0x6E13ED, { 0x8B, 0x15 } ) &&
			MemEquals( 0x6E141F, { 0xA1 } )
		)
	{
		using namespace LightbeamFix;

		std::array<uintptr_t, 3> doHeadLightBeam = { 0x6A2EDA, 0x6A2EF2, 0x6BDE80 };
		CVehicle::HookEach_DoHeadLightBeam(doHeadLightBeam, InterceptCall);

		Patch( 0x6E0F37 + 2, &RenderStateWrapper<rwRENDERSTATEZWRITEENABLE>::PushStatePPtr );
		Patch( 0x6E0F63 + 1, &RenderStateWrapper<rwRENDERSTATEZTESTENABLE>::PushStatePPtr );
		Patch( 0x6E0F6F + 2, &RenderStateWrapper<rwRENDERSTATEVERTEXALPHAENABLE>::PushStatePPtr );
		Patch( 0x6E0F7C + 2, &RenderStateWrapper<rwRENDERSTATESRCBLEND>::PushStatePPtr );
		Patch( 0x6E0F89 + 1, &RenderStateWrapper<rwRENDERSTATEDESTBLEND>::PushStatePPtr );
		Patch( 0x6E0F95 + 2, &RenderStateWrapper<rwRENDERSTATESHADEMODE>::PushStatePPtr );
		// rwRENDERSTATETEXTURERASTER not saved
		Patch( 0x6E0FAF + 1, &RenderStateWrapper<rwRENDERSTATECULLMODE>::PushStatePPtr );
		Patch( 0x6E0FBB + 2, &RenderStateWrapper<rwRENDERSTATEALPHATESTFUNCTION>::PushStatePPtr );
		Patch( 0x6E0FCB + 2, &RenderStateWrapper<rwRENDERSTATEALPHATESTFUNCTIONREF>::PushStatePPtr );

		// rwRENDERSTATETEXTURERASTER not saved
		Patch( 0x6E13E0 + 2, &RenderStateWrapper<rwRENDERSTATEZWRITEENABLE>::PopStatePPtr );
		Patch( 0x6E13ED + 2, &RenderStateWrapper<rwRENDERSTATEZTESTENABLE>::PopStatePPtr );
		Patch( 0x6E13FA + 1, &RenderStateWrapper<rwRENDERSTATESRCBLEND>::PopStatePPtr );
		Patch( 0x6E1406 + 2, &RenderStateWrapper<rwRENDERSTATEDESTBLEND>::PopStatePPtr );
		Patch( 0x6E1413 + 2, &RenderStateWrapper<rwRENDERSTATEVERTEXALPHAENABLE>::PopStatePPtr );
		Patch( 0x6E141F + 1, &RenderStateWrapper<rwRENDERSTATECULLMODE>::PopStatePPtr );

		// Debug override registered in delayed patches
		hookedSuccessfully = true;
	}

	// PS2 SUN!!!!!!!!!!!!!!!!!
	Nop(0x6FB17C, 3);

#if defined EXPAND_ALPHA_ENTITY_LISTS
	// Bigger alpha entity lists
	Patch<DWORD>(0x733B05, EXPAND_ALPHA_ENTITY_LISTS * 20);
	Patch<DWORD>(0x733B55, EXPAND_ALPHA_ENTITY_LISTS * 20);
#endif

	// Fix lights not resetting the material attributes correctly
	InjectHook(0x4C830C, LightMaterialsFix, HookType::Call);

	// Unlocked widescreen resolutions
	{
		// Advanced Display Options
		Nop(0x745B71, 6); // Skip width check
		Nop(0x745B81, 6); // Skip height check
		if (MemEquals(0x745B96, { 0x7Bu })) // SA-MP stomps over this entire block of code
		{
			Patch<uint8_t>(0x745B96, 0xEB); // Skip AR check
		}
		Nop(0x745BFC, 2); // Skip VRAM check

		// Resolution selection dialog
		Nop(0x74596C, 6); // Skip width check
		Nop(0x74597A, 6); // Skip height check
		if (MemEquals(0x7459D0, { 0x7Bu })) // SA-MP stomps over this entire block of code
		{
			Patch<uint8_t>(0x7459D0, 0xEB); // Skip AR check
		}
	}

	// Heap corruption fix
	Nop(0x5C25D3, 5);

	// User Tracks fix
	if (UserTracksChanges::HasGameBindings())
	{
		using namespace UserTracksChanges;

		ReadCall( 0x4D9B66, SetVolume );
		InjectHook(0x4D9B66, UserTracksFix);
		InjectHook(0x4D9BB5, 0x4F2FD0);

		// FLAC support
		if (CAEDataStream::HasGameBindings())
		{
			InjectHook(0x4F373D, LoadFLAC, HookType::Jump);
			InjectHook(0x57BEFE, FLACInit);
			InjectHook(0x4F3787, CAEWaveDecoderInit);

			Patch<WORD>(0x4F376A, 0x18EB);
			//Patch<BYTE>(0x4F378F, sizeof(CAEWaveDecoder));
			Patch<const void*>(0x4F3210, UserTrackExtensions);
			Patch<const void*>(0x4F3241, &UserTrackExtensions->Codec);
			Patch<const void*>(0x4F35E7, &UserTrackExtensions[1].Codec);
			Patch<BYTE>(0x4F322D, sizeof(UserTrackExtensions));
		}
	}

	// Impound garages working correctly
	InjectHook(0x425179, 0x448990); // CGarages::IsPointWithinAnyGarage
	InjectHook(0x425369, 0x448990); // CGarages::IsPointWithinAnyGarage
	InjectHook(0x425411, 0x448990); // CGarages::IsPointWithinAnyGarage

	// Impounding after busted works
	Nop(0x443292, 5);

	// Mouse rotates an airborne car only with Steer with Mouse option enabled
	bool*	bEnableMouseSteering = *(bool**)0x6AD7AD; // CVehicle::m_bEnableMouseSteering
	Patch<bool*>(0x6B4EC0, bEnableMouseSteering);
	Patch<bool*>(0x6CE827, bEnableMouseSteering);

	// Patched CAutomobile::Fix
	// misc_x parts don't get reset (Bandito fix), Towtruck's bouncing panel is not reset
	if (HasGameBindings_AutomobileFix())
	{
		Patch<WORD>(0x6A34C9, 0x5EEB);
		Patch<DWORD>(0x6A3555, 0x5E5FCF8B);
		Patch<DWORD>(0x6A3559, 0x448B5B5D);
		Patch<DWORD>(0x6A355D, 0x89644824);
		Patch<DWORD>(0x6A3561, 5);
		Patch<DWORD>(0x6A3565, 0x54C48300);
		InjectHook(0x6A3569, &CAutomobile::Fix_SilentPatch, HookType::Jump);
	}

	// Patched CPlane::Fix
	// Reset bouncing panels, except for Vortex
	Patch<DWORD>(0x6CAC05, 0x5E5FCF8B);
	InjectHook(0x6CAC09, &CPlane::Fix_SilentPatch, HookType::Jump);

	// Weapon icon fix (crosshairs mess up rwRENDERSTATEZWRITEENABLE)
	Nop(0x58E210, 3);
	Nop(0x58EAB7, 3);
	Nop(0x58EAE1, 3);

	// Zones fix
	InjectHook(0x572130, GetCurrentZoneLockedOrUnlocked, HookType::Jump);

	// Bilinear filtering for license plates
	//Patch<BYTE>(0x6FD528, rwFILTERLINEAR);
	Patch<BYTE>(0x6FDF47, rwFILTERLINEAR);

	// -//- Roadsign manager
	//Patch<BYTE>(0x6FE147, rwFILTERLINEAR);

	// Bilinear filtering with mipmaps for weapon icons
	Patch<BYTE>(0x58D7DA, rwFILTERMIPLINEAR);

	// Directional multiplier value from timecyc.dat properly using floats
	Patch<WORD>(0x5BBFC9, 0x14EB);

	// Directional multiplier defaults to 1.0f, and work around a broken RAINY_COUNTRYSIDE line in PC timecyc.dat
	{
		using namespace TimecycDatMissingDataFix;
		InterceptCall(0x5BBCE2, orgSscanf, sscanf_TimecycLine);
	}

	// All lights get casted at vehicles
	Patch<BYTE>(0x5D9A88, 8);
	Patch<BYTE>(0x5D9A91, 8);
	Patch<BYTE>(0x5D9F1F, 8);

	// 6 extra directionals on Medium and higher
	// push eax
	// call GetMaxExtraDirectionals
	// add esp, 4
	// mov ebx, eax
	// nop
	if (g_fx.Ensure())
	{
		Patch<uint8_t>( 0x735881, 0x50 );
		InjectHook( 0x735881 + 1, GetMaxExtraDirectionals, HookType::Call );
		Patch( 0x735881 + 6, { 0x83, 0xC4, 0x04, 0x8B, 0xD8 } );
		Nop( 0x735881 + 11, 3 );
	}

	// Default resolution to native resolution
	const auto [width, height] = GetDesktopResolution();
	sprintf_s(aNoDesktopMode, "Cannot find %ux%ux32 video mode", width, height);

	if (width != 0 && height != 0)
	{
		Patch<DWORD>(0x746363, width);
		Patch<DWORD>(0x746368, height);
		Patch<const char*>(0x7463C8, aNoDesktopMode);
	}

	// Corrected Map screen 1px issue
	Patch<float>(0x575DE7, -0.5f);
	Patch<float>(0x575DA7, -0.5f);
	Patch<float>(0x575DAF, -0.5f);
	Patch<float>(0x575D5C, -0.5f);
	Patch<float>(0x575CDA, -0.5f);
	Patch<float>(0x575D0C, -0.5f);

	// Cars drive on water cheat
	Patch<DWORD>(&(*(DWORD**)0x438513)[34], 0xE5FC92C3);

	// No DirectPlay dependency
	// mov eax, 0x900
	Patch<BYTE>(AddressByRegion_10<DWORD>(0x74754A), 0xB8);
	Patch<DWORD>(AddressByRegion_10<DWORD>(0x74754B), 0x900);

	// SHGetFolderPath on User Files
	if (ppUserFilesDir.Ensure())
	{
		InjectHook(0x744FB0, GetMyDocumentsPathSA, HookType::Jump);
	}

	// Fixed muzzleflash not showing from last bullet
	// nop \ test al, al \ jz
	Nop(0x61ECDC, 6);
	Patch(0x61ECE2, { 0x84, 0xC0, 0x74 });

	// Proper randomizations
	{
		using namespace ConsoleRandomness;

		InjectHook(0x44E82E, rand31); // Missing ped paths
		InjectHook(0x44ECEE, rand31); // Missing ped paths
		InjectHook(0x666EA0, rand31); // Prostitutes
	}

	// Help boxes showing with big message
	// Game seems to assume they can show together
	Nop(0x58BA8F, 6);

	// Fixed lens flare
	if (LensFlareRenderFix::HasGameBindings())
	{
		using namespace LensFlareRenderFix;

		Patch<DWORD>(0x70F45A, 0); // TODO: Is this needed?
		Patch<BYTE>(0x6FB621, 0xC3); // nop CSprite::FlushSpriteBuffer
		// Add CSprite::FlushSpriteBuffer, jmp loc_6FB605 at the bottom of the function
		Patch<BYTE>(0x6FB600, 0x21);
		InjectHook(0x6FB622, 0x70CF20, HookType::Call);
		Patch<WORD>(0x6FB627, 0xDCEB);

		// nop / mov eax, offset FlushLensSwitchZ
		Patch<WORD>(0x6FB476, 0xB990);
		Patch(0x6FB478, &FlushLensSwitchZ);
		Patch<WORD>(0x6FB480, 0xD1FF);
		Nop(0x6FB482, 1);

		// nop / mov ecx, offset InitBufferSwitchZ
		Patch<WORD>(0x6FAF28, 0xB990);
		Patch(0x6FAF2A, &InitBufferSwitchZ);
		Patch<WORD>(0x6FAF32, 0xD1FF);
		Nop(0x6FAF34, 1);
	}

	// Y axis sensitivity fix
	// By ThirteenAG
	float* sens = *(float**)0x50F03C;
	Patch<const void*>(0x50EB70 + 0x4D6 + 0x2, sens);
	Patch<const void*>(0x50F970 + 0x1B6 + 0x2, sens);
	Patch<const void*>(0x5105C0 + 0x666 + 0x2, sens);
	Patch<const void*>(0x511B50 + 0x2B8 + 0x2, sens);
	Patch<const void*>(0x521500 + 0xD8C + 0x2, sens);

	// Don't lock mouse Y axis during fadeins
	Patch<WORD>(0x50FBB4, 0x27EB);
	Patch<WORD>(0x510512, 0xE990);
	InjectHook(0x524071, 0x524139, HookType::Jump);

	// Fixed mirrors crash
	// TODO: Change when short jumps are supported
	// test eax, eax / je 0727203 / add esp, 4
	Patch( 0x7271CB, { 0x85, 0xC0, 0x74, 0x34, 0x83, 0xC4, 0x04 } );

	// Mirrors depth fix & bumped quality
	if (MirrorsDepthFix::HasGameBindings())
	{
		using namespace MirrorsDepthFix;
		InjectHook(0x72701D, CreateMirrorBuffers);
	}

	// Fixed MSAA options
	{
		using namespace MSAAFixes;

		Patch<BYTE>(0x57D126, 0xEB);
		Nop(0x57D0E8, 2);

		Patch<BYTE>(AddressByRegion_10<BYTE*>(0x7F6C9B), 0xEB);
		Patch<BYTE>(AddressByRegion_10<BYTE*>(0x7F60C6), 0xEB);
		Patch(AddressByRegion_10<BYTE*>(0x7F6683), { 0x90, 0xE9 });

		std::array<uintptr_t, 2> getMaxMultiSamplingLevels = { 0x57D136, 0x57D0EA };
		HookEach_GetMaxMultiSamplingLevels(getMaxMultiSamplingLevels, InterceptCall);

		std::array<uintptr_t, 4> setOrChangeMultiSamplingLevels = { 0x5744FD, 0x57D162, 0x57D2A6, 0x746350 };
		HookEach_SetOrChangeMultiSamplingLevels(setOrChangeMultiSamplingLevels, InterceptCall);

		Nop(0x57A0FC, 1);
		InjectHook(0x57A0FD, MSAAText, HookType::Call);
	}

	// Fixed car collisions - car you're hitting gets proper damage now
	InjectHook(0x5428EA, FixedCarDamage, HookType::Call);


	// Car explosion crash with multimonitor
	// Unitialized collision data breaking stencil shadows
	{
		using namespace UnitializedCollisionDataFix;

		VP::InterceptCall(ModCompat::Utils::GetFunctionAddrIfRerouted(0x40F870) + 0x63, orgMemMgrMalloc, CollisionData_MallocAndInit);

		std::array<uintptr_t, 2> newAndInit = {
			ModCompat::Utils::GetFunctionAddrIfRerouted(0x40F740) + 0xC,
			ModCompat::Utils::GetFunctionAddrIfRerouted(0x40F810) + 0xD,
		};
		HookEach_CollisionDataNew(newAndInit, InterceptCall);
	}


	// Crash when entering advanced display options on a dual monitor machine after:
	// - starting game on primary monitor in maximum resolution, exiting,
	// starting again in maximum resolution on secondary monitor.
	// Secondary monitor maximum resolution had to be greater than maximum resolution of primary monitor.
	ReadCall( 0x745B1E, orgGetNumVideoModes );
	InjectHook(0x745B1E, GetNumVideoModes_Store);
	InjectHook(0x745A81, GetNumVideoModes_Retrieve);


	// Fixed escalators crash
	if (WorldRemove.Ensure())
	{
		ReadCall( 0x7185B5, orgEscalatorsUpdate );
		InjectHook(0x7185B5, UpdateEscalators);
		InjectHook(0x71791F, &CEscalator::SwitchOffNoRemove);
	}


	// Don't allocate constant memory for stencil shadows every frame
	if (s_pStencilShadowsPad.Ensure())
	{
		InjectHook(0x711DD5, StencilShadowAlloc, HookType::Call);
		Nop(0x711E0D, 3);
		Patch(0x711DDA, { 0xEB, 0x2C });
		Patch(0x711E5F, { 0x5F, 0x5D, 0xC3 });	// pop edi, pop ebp, ret
	}


	// "Streaming memory bug" fix
	InjectHook(0x4C51A9, GTARtAnimInterpolatorSetCurrentAnim);


	// Fixed ammo for melee weapons in cheats
	Patch<BYTE>(0x43890B+1, 1); // knife
	Patch<BYTE>(0x4389F8+1, 1); // knife
	Patch<BYTE>(0x438B9F+1, 1); // chainsaw
	Patch<BYTE>(0x438C58+1, 1); // chainsaw
	Patch<BYTE>(0x4395C8+1, 1); // parachute

	Patch<BYTE>(0x439F1F, 0x53); // katana
	Patch<WORD>(0x439F20, 0x016A);


	// Fixed police scanner names
	char*			pScannerNames = *(char**)0x4E72D4;
	strcpy_s(pScannerNames + (8*113), 8, "WESTP");
	strcpy_s(pScannerNames + (8*134), 8, "????");


	// AI accuracy issue
	Nop(0x73B3AE, 1);
	InjectHook( 0x73B3AE + 1, WeaponRangeMult_VehicleCheck, HookType::Call );


	// New timers fix
	InjectHook( 0x561C32, asmTimers_ftol_PauseMode );
	InjectHook( 0x561902, asmTimers_ftol_NonClipped );
	InjectHook( 0x56191A, asmTimers_ftol );
	InjectHook( 0x46A036, asmTimers_SCMdelta );


	// Don't catch WM_SYSKEYDOWN and WM_SYSKEYUP (fixes Alt+F4)
	InjectHook( AddressByRegion_10<int>(0x748220), AddressByRegion_10<int>(0x748446), HookType::Jump );
	Patch<uint8_t>( AddressByRegion_10<int>(0x7481E3), 0x5C ); // esi -> ebx
	Patch<uint8_t>( AddressByRegion_10<int>(0x7481EA), 0x53 ); // esi -> ebx
	Patch<uint8_t>( AddressByRegion_10<int>(0x74820D), 0xFB ); // esi -> ebx
	Patch<int8_t>( AddressByRegion_10<int>(0x7481EF), 0x54-0x3C ); // use stack space for new lParam
	Patch<int8_t>( AddressByRegion_10<int>(0x748200), 0x4C-0x3C ); // use stack space for new lParam
	Patch<int8_t>( AddressByRegion_10<int>(0x748214), 0x4C-0x3C ); // use stack space for new lParam

	InjectHook( AddressByRegion_10<int>(0x74826A), AddressByRegion_10<int>(0x748446), HookType::Jump );
	Patch<uint8_t>( AddressByRegion_10<int>(0x74822D), 0x5C ); // esi -> ebx
	Patch<uint8_t>( AddressByRegion_10<int>(0x748234), 0x53 ); // esi -> ebx
	Patch<uint8_t>( AddressByRegion_10<int>(0x748257), 0xFB ); // esi -> ebx
	Patch<int8_t>( AddressByRegion_10<int>(0x748239), 0x54-0x3C ); // use stack space for new lParam
	Patch<int8_t>( AddressByRegion_10<int>(0x74824A), 0x4C-0x3C ); // use stack space for new lParam
	Patch<int8_t>( AddressByRegion_10<int>(0x74825E), 0x4C-0x3C ); // use stack space for new lParam


	// FuckCarCompletely not fixing panels
	Nop(0x6C268D, 3);


	// 014C cargen counter fix (by spaceeinstein)
	Patch<uint8_t>( 0x06F3E2C + 1, 0xBF ); // movzx ecx, ax -> movsx ecx, ax
	Patch<uint8_t>( 0x6F3E32, 0x74 ); // jge -> jz


	// Linear filtering on script sprites
	ReadCall( 0x58C092, orgDrawScriptSpritesAndRectangles );
	InjectHook( 0x58C092, DrawScriptSpritesAndRectangles );


	// Properly initialize all CVehicleModelInfo fields
	InterceptCall(0x4C7633, orgVehicleModelInfoInit, VehicleModelInfoInit);

	// Animated Phoenix hood scoop
	if (HasGameBindings_ExtraAutomobileAnimations())
	{
		auto* automobilePreRender = (*(decltype(CAutomobile::orgAutomobilePreRender<0>)**)(0x6B0AD2 + 2)) + 17;
		CAutomobile::orgAutomobilePreRender<0> = *automobilePreRender;
		Patch(automobilePreRender, &CAutomobile::PreRender_SilentPatch<0>);

		std::array<uintptr_t, 3> preRender = { 0x6C7E7A, 0x6CEAEC, 0x6CFADC };
		CAutomobile::HookEach_PreRender(preRender, InterceptCall);
	}

	// Extra animations for planes
	auto* planePreRender = (*(decltype(CPlane::orgPlanePreRender)**)(0x6C8E5A + 2)) + 17;
	CPlane::orgPlanePreRender = *planePreRender;
	Patch(planePreRender, &CPlane::PreRender_Stub);


	// Stop BF Injection/Bandito/Hotknife rotating engine components when engine is off
	Patch<const void*>(0x6AC2BE + 2, &CAutomobile::ms_engineCompSpeed);
	Patch<const void*>(0x6ACB91 + 2, &CAutomobile::ms_engineCompSpeed);


	// Make freeing temp objects more aggressive to fix vending crash
	if (HasGameBindings_TryToFreeUpTempObjects())
	{
		InjectHook( 0x5A1840, CObject::TryToFreeUpTempObjects_SilentPatch, HookType::Jump );
	}


	// Remove FILE_FLAG_NO_BUFFERING from CdStreams
	Patch<uint8_t>( 0x406BC6, 0xEB );


	// Proper metric-imperial conversion constants
	static const float METERS_TO_FEET = 3.280839895f;
	Patch<const void*>( 0x55942F + 2, &METERS_TO_FEET );
	Patch<const void*>( 0x55AA96 + 2, &METERS_TO_FEET );


	// Fixed impounding of random vehicles (because CVehicle::~CVehicle doesn't remove cars from apCarsToKeep)
	if (RemoveFromInterestingVehicleList.Ensure())
	{
		ReadCall( 0x6E2B6E, orgRecordVehicleDeleted );
		InjectHook( 0x6E2B6E, RecordVehicleDeleted_AndRemoveFromVehicleList );
	}


	// Don't include an extra D3DLIGHT on vehicles since we fixed directional already
	// By aap
	Patch<float>(0x5D88D1 + 6, 0);
	Patch<float>(0x5D88DB + 6, 0);
	Patch<float>(0x5D88E5 + 6, 0);

	Patch<float>(0x5D88F9 + 6, 0);
	Patch<float>(0x5D8903 + 6, 0);
	Patch<float>(0x5D890D + 6, 0);


	// Fixed CAEAudioUtility timers - not typecasting to float so we're not losing precision after X days of PC uptime
	// Also fixed integer division by zero
	{
		::QueryPerformanceFrequency( &UtilsFrequency );
		::QueryPerformanceCounter( &UtilsStartTime );

		Patch( 0x5B9868 + 2, &pAudioUtilsFrequency );
		InjectHook( 0x5B9886, AudioUtilsGetStartTime );
		InjectHook( 0x4D9E80, AudioUtilsGetCurrentTimeInMs, HookType::Jump );
	}

	// Car generators placed in interiors visible everywhere
	InjectHook( 0x6F3B30, &CEntity::SetPositionAndAreaCode );


	// Fixed bomb ownership/bombs saving for bikes
	if (CStoredCar::HasGameBindings_RestoreCar())
	{
		std::array<uintptr_t, 2> restoreCar = {
			ModCompat::Utils::GetFunctionAddrIfRerouted(0x448550) + 0x1A,
			ModCompat::Utils::GetFunctionAddrIfRerouted(0x4485C0) + 0x1B,
		};
		CStoredCar::HookEach_RestoreCar(restoreCar, VP::InterceptCall);
	}


	// unnamed CdStream semaphore
	Patch( 0x406945, { 0x6A, 0x00 } ); // push 0 \ nop
	Nop( 0x406945 + 2, 3 );


	// Correct streaming when using RC vehicles
	if (HasGameBindings_FindPlayer())
	{
		InjectHook( 0x55574B, FindPlayerEntityWithRC );
		InjectHook( 0x5557C3, FindPlayerVehicle_RCWrap );
	}


	// Fixed triangle above recruitable peds' heads
	Patch<uint8_t>( 0x60BC52 + 2, 8 ); // GANG2


	// Fixed ammo from SCM
	{
		ReadCall( 0x47D335, CPed::orgGiveWeapon );
		InjectHook( 0x47D335, &CPed::GiveWeapon_SP );
	}

	// Fixed bicycle on fire - instead of CJ being set on fire, bicycle's driver is
	{
		using namespace BicycleFire;

		Patch( 0x53A9A7, { 0x90, 0x57 } ); // nop \ push edi
		InjectHook( 0x53A9A7 + 2, GetVehicleDriver );

		InjectHook( 0x53A990, DoStuffToGoOnFire_Noop );

		InterceptCall( 0x53A9B7, orgStartFire, StartFire_NullEntityCheck );
	}


	// Decreased keyboard input latency
	{
		using namespace KeyboardInputFix;

		NewKeyState = *(void**)( 0x541E21 + 1 );
		OldKeyState = *(void**)( 0x541E26 + 1 );
		TempKeyState = *(void**)( 0x541E32 + 1 );
		objSize = *(uint32_t*)( 0x541E1C + 1 ) * 4;

		ReadCall( 0x541DEB, orgClearSimButtonPressCheckers );

		// Only hook if this call takes to somewhere in gta_sa.exe, else bail out since it's been tampered with
		if ( hInstance == ModCompat::Utils::GetModuleHandleFromAddress(orgClearSimButtonPressCheckers) )
		{
			InjectHook( 0x541DEB, ClearSimButtonPressCheckers );
			Nop( 0x541E2B, 2 );
			Nop( 0x541E3C, 2 );
		}
	}


	// Fixed handling.cfg name matching (names don't need unique prefixes anymore)
	{
		using namespace HandlingNameLoadFix;

		InjectHook( 0x6F4F58, strncpy_Fix );
		InjectHook( 0x6F4F64, strncmp_Fix );
	}


	// Firela animations
	{
		using namespace FirelaHook;

		UpdateMovingCollisionJmp = 0x6B200F;
		HydraulicControlJmpBack = 0x6B1FBF + 10;
		InjectHook( 0x6B1FBF, TestFirelaAndFlags, HookType::Jump );

		FollowCarCamNoMovement = 0x52551E;
		FollowCarCamJmpBack = 0x5254F6 + 6;
		InjectHook( 0x5254F6, CamControlFirela, HookType::Jump );
	}


	// Double artict3 trailer
	if (HasGameBindings_GetTowBarPos())
	{
		auto* trailerTowBarPos = (*(decltype(CTrailer::orgGetTowBarPos)**)(0x6D03FD + 2)) + 60;
		CTrailer::orgGetTowBarPos = *trailerTowBarPos;
		Patch(trailerTowBarPos, &CTrailer::GetTowBarPos_Stub);
	}


	// DFT-30 wheel, Sweeper brushes and other typos in hierarchy
	{
		using namespace HierarchyTypoFix;

		InterceptCall(0x4C5311, orgStrcasecmp, strcasecmp);
	}


	// Tug tow bar (misc_b instead of misc_a
	Nop( 0x6AF2CC, 1 );
	InjectHook( 0x6AF2CC + 1, &CAutomobile::GetTowBarFrame, HookType::Call );


	// Play passenger's voice lines when killing peds with car, not only when hitting them damages player's vehicle
	if (CPed::SayFunc.Ensure() && HasGameBindings_FindPlayer())
	{
		InterceptCall(0x5F05CA, CVehicle::orgPlayPedHitSample_GetColModel, &CVehicle::PlayPedHitSample_GetColModel);
	}

	// Prevent samples from playing where they used to, so passengers don't comment on gently pushing peds
	InterceptCall(0x6A8298, CPed::orgSay, &CPed::Say_SampleBlackList<CONTEXT_GLOBAL_CAR_HIT_PED>);


	// Reset variables on New Game
	{
		using namespace VariableResets;

		std::array<uintptr_t, 2> reInitGameObjectVariables = { 0x53C6DB, 0x53C76D };
		HookEach_ReInitGameObjectVariables(reInitGameObjectVariables, InterceptCall);

		InterceptCall(0x5B89E4, orgLoadPickup, LoadPickup_SaveLine);
		InterceptCall(0x5B89EE, orgLoadCarGenerator, LoadCarGenerator_SaveLine);
		InterceptCall(0x5B89F9, orgLoadStuntJump, LoadStuntJump_SaveLine);

		// Variables to reset
		AddVariableToReset( (bool**)(0x63E8D8+1) ); // CPlayerPed::bHasDisplayedPlayerQuitEnterCarHelpText
		AddVariableToReset( (bool**)(0x44AC97+1) ); // CGarages::RespraysAreFree
		AddVariableToReset( (bool**)(0x44B49D+1) ); // CGarages::BombsAreFree

		AddVariableToReset( (int**)(0x42131F + 2) ); // CCarCtrl::LastTimeFireTruckCreated
		AddVariableToReset( (int**)(0x421319 + 2) ); // CCarCtrl::LastTimeAmbulanceCreated

		AddVariableToReset( (int**)(0x55C843 + 1) ); // CStats::m_CycleSkillCounter
		AddVariableToReset( (int**)(0x55CA39 + 1) ); // CStats::m_SwimUnderWaterCounter
		AddVariableToReset( (int**)(0x55CF3E + 2) ); // CStats::m_WeaponCounter
		AddVariableToReset( (int**)(0x55CF2A + 2) ); // CStats::m_LastWeaponTypeFired
		AddVariableToReset( (int**)(0x55CFC1 + 1) ); // CStats::m_DeathCounter
		AddVariableToReset( (int**)(0x55C5E5 + 1) ); // CStats::m_MaxHealthCounter
		AddVariableToReset( (int**)(0x55D043 + 1) ); // CStats::m_AddToHealthCounter

		// Non-zero inits still need to be done
		AddVariableToReset( (TimeNextMadDriverChaseCreated_t<float>**)(0x421369 + 2) ); // CCarCtrl::TimeNextMadDriverChaseCreated

		AddVariableToReset( (ResetToTrue_t**)(0x4758A4 + 2) ); // CGameLogic::bPenaltyForDeathApplies
		AddVariableToReset( (ResetToTrue_t**)(0x4758C4 + 1) ); // CGameLogic::bPenaltyForArrestApplies
	}

	// Don't clean the car BEFORE Pay 'n Spray doors close, as it gets cleaned later again anyway!
	Nop( 0x44ACDC, 6 );


	// Locale based metric/imperial system
	{
		using namespace Localization;

		InjectHook( 0x56D220, IsMetric_LocaleBased, HookType::Jump );
	}


	// Fix paintjobs vanishing after opening/closing garage without rendering the car 
	if (ms_modelInfoPtrs.Ensure())
	{
		InjectHook( 0x6D0B70, &CVehicle::GetRemapIndex, HookType::Jump );
	}


	// Re-introduce corona rotation on PC, like it is in III/VC/SA PS2
	{
		using namespace CoronaRotationFix;

		// Remove *= 20.0f from recipz to retrieve the original value for later
		Nop( 0x6FB277, 6 );

		ReadCall( 0x6FB2E6, orgRenderOneXLUSprite_Rotate_Aspect );
		InjectHook( 0x6FB2E6, RenderOneXLUSprite_Rotate_Aspect_SilentPatch );
	}


	// Fixed static shadows not rendering under fire and pickups
	{
		using namespace StaticShadowAlphaFix;

		ReadCall( 0x53E0C3, orgRenderStaticShadows );
		InjectHook( 0x53E0C3, RenderStaticShadows_StateFix );

		// Stored shadows conflict with SSE and are patched only when it's not installed
	}


	// Reset requested extras if created vehicle has no extras
	// Fixes e.g. lightless taxis
	if (HasGameBindings_ResetCompsForNoExtras())
	{
		InjectHook( 0x4C97B1, CVehicleModelInfo::ResetCompsForNoExtras, HookType::Call );
		Nop( 0x4C97B1 + 5, 9 );
	}


	// Allow extra6 to be picked with component rule 4 (any)
	Patch<uint32_t>( 0x4C8010 + 4, 6 );


	// Disallow moving cam up/down with mouse when looking back/left/right in vehicle
	if (FollowCarMouseCamFix::HasGameBindings())
	{
		using namespace FollowCarMouseCamFix;

		orgUseMouse3rdPerson = *(bool**)(0x525615 + 1);
		Patch( 0x525615 + 1, &useMouseAndLooksForwards );

		ReadCall( 0x5245E4, orgGetPad );
		InjectHook( 0x5245E4, getPadAndSetFlag );
	}


	// Display stats in kg as floats (they pass a float and intend to display an integer)
	Patch<const char*>( 0x55A954 + 1, "%.0fkg" );
	Patch<const char*>( 0x5593E4 + 1, "%.0fkg" );


	// Add wind animations when driving a Quadbike
	// By Wesser
	InjectHook(0x5E69BC, &CVehicle::IsOpenTopCarOrQuadbike, HookType::Call);
	Nop(0x5E69BC + 5, 3);


	// Tie handlebar movement to the steering animations on Quadbike, fixes odd animation interpolations at low speeds
	// By Wesser
	Nop(0x6B7932, 1);
	InjectHook(0x6B7932+1, &QuadbikeHandlebarAnims::ProcessRiderAnims_FixInterp, HookType::Call);


	// Modify the radio station change animation to only affect the right hand, and disable it on the Kart
	// By Wesser, improved by B1ack_Wh1te
	if (RadioStationChangeAnimBlending::HasGameBindings())
	{
		using namespace RadioStationChangeAnimBlending;

		InterceptCall(0x6DF4F4, orgAnimManagerBlendAnimation, AnimManagerBlendAnimation_DisableBones);
	}


	// Fix a memory leak when taking photos
	{
		using namespace CameraMemoryLeakFix;

		InjectHook(0x7453CE, psGrabScreen_UnlockAndReleaseSurface, HookType::Jump);
		InjectHook(0x7453D6, psGrabScreen_UnlockAndReleaseSurface, HookType::Jump);
	}


	// Fix crosshair issues when sniper rifle is equipped and a photo is taken by a gang member
	// By Wesser
	if (CameraCrosshairFix::HasGameBindings())
	{
		using namespace CameraCrosshairFix;

		InterceptCall(0x58E842, orgGetWeaponInfo, GetWeaponInfo_OrCamera);
	}


	// Cancel the Drive By task of biker cops when losing the wanted level
	// DRM-obfuscated, so exceptionally use patterns
	try
	{
		using namespace BikerCopsDriveByFix;
		using namespace hook::txn;

		uintptr_t backToCruisingIfNoWantedLevel = ModCompat::Utils::GetFunctionAddrIfRerouted(0x41BFA0);
		auto joinCarWithRoadSystem = get_pattern({{ backToCruisingIfNoWantedLevel, backToCruisingIfNoWantedLevel + 0x100 }},
						"56 E8 ? ? ? ? 8A 96 2D 04 00 00", 1);

		InterceptCall(joinCarWithRoadSystem, orgJoinCarWithRoadSystem, JoinCarWithRoadSystem_AbortDriveByTask);
	}
	TXN_CATCH();


	// Fix miscolored racing checkpoints if no other marker was drawn before them
	{
		using namespace RacingCheckpointsRender;

		InterceptCall(0x721520, orgRpClumpRender, RpClumpRender_SetLitFlag);
	}


	// Correct an improperly decrypted CPlayerPedData::operator= that broke gang recruiting after activating replays
	// Only broken in the HOODLUM EXE and the compact EXE that carried over the bug
	// By Wesser
	{
		using namespace PlayerPedDataAssignment;

		uintptr_t placeToPatch = ModCompat::Utils::GetFunctionAddrIfRerouted(0x45C4B0) + 0x5D;

		// If we're overwriting actual meaningful instructions and not NOPs, use a different wrapper
		if (MemEquals(placeToPatch, { 0x90, 0x90, 0x90, 0x90, 0x90 }))
		{
			InjectHook(placeToPatch, AssignmentOp_Hoodlum, HookType::Call);
		}
		// Only patch compact if it's not been fixed already. No such executable exists right now, but this might change in the future
		else if (MemEquals(placeToPatch, { 0x33, 0xD6, 0x81, 0xE2, 0x00, 0x02, 0x00, 0x00 }))
		{
			InjectHook(placeToPatch, AssignmentOp_Compact, HookType::Call);
			Nop(placeToPatch + 5, 3);
		}
	}


	// Delay destroying of cigarettes/bottles held by NPCs so it does not potentially corrupt the moving list

	// CWorld::Process processes all entries in the moving list, calling ProcessControl on them.
	// CPlayerPed::ProcessControl handles the gang recruitment which in turn can result in homies dropping cigarettes or bottles.
	// When this happens, they are destroyed -immediately-. If those props are in the moving list right after the PlayerPed,
	// this corrupts a pre-cached node->next pointer and references an already freed entity.
	// To fix this, queue the entity for a delayed destruction instead of destroying immediately,
	// and let it destroy itself in CWorld::Process later.

	// or [esi+1Ch], 800h // bRemoveFromWorld
	// (The entity reference is already cleared for us, no need to do it)
	// jmp 5E03EC
	Patch(0x5E03D4, { 0x81, 0x4E, 0x1C, 0x00, 0x08, 0x00, 0x00, 0xEB, 0x0F });


	// Spawn lapdm1 (biker cop) correctly if the script requests one with PEDTYPE_COP
	// By Wesser
	{
		using namespace GetCorrectPedModel_Lapdm1;

		Patch(0x464FC8, &BikerCop_Retail);
	}


	// Only allow impounding cars and bikes (and their subclasses), as impounding helicopters, planes, boats makes no sense
	{
		using namespace RestrictImpoundVehicleTypes;

		std::array<uint32_t, 2> isThisVehicleInteresting = { 0x566794, 0x56A378 };
		HookEach_ShouldImpound(isThisVehicleInteresting, InterceptCall);
	}


	// Fix PlayerPed replay crashes
	// 1. Crash when starting a mocap cutscene after playing a replay wearing different clothes to the ones CJ has currently
	// 2. Crash when playing back a replay with a different motion group animation (fat/muscular/normal) than the current one
	if (ReplayPlayerPedCrashFixes::HasGameBindings())
	{
		using namespace ReplayPlayerPedCrashFixes;

		InterceptCall(0x45F060, orgRestoreStuffFromMem, RestoreStuffFromMem_RebuildPlayer);

		bool HoodlumPatched = false;
		if (*reinterpret_cast<const uint8_t*>(0x45CEA0) == 0xE9)
		{
			// Since this function differs between EU and US Hoodlum, exceptionally use patterns
			using namespace hook::txn;

			uintptr_t DealWithNewPedPacket_Obfuscated;
			ReadCall(0x45CEA0, DealWithNewPedPacket_Obfuscated);
			if (ModCompat::Utils::GetModuleHandleFromAddress(DealWithNewPedPacket_Obfuscated) == hInstance) try
			{
				auto DealWithNewPedPacket = get_pattern({{ DealWithNewPedPacket_Obfuscated, DealWithNewPedPacket_Obfuscated + 0x200 }},
					"6A 01 56 E8 ? ? ? ? 83 C4 10", 3);

				VP::InterceptCall(DealWithNewPedPacket, orgRebuildPlayer, RebuildPlayer_LoadAllMotionGroupAnims);
				HoodlumPatched = true;
			}
			TXN_CATCH();
		}

		if (!HoodlumPatched)
		{
			InterceptCall(0x45CF87, orgRebuildPlayer, RebuildPlayer_LoadAllMotionGroupAnims);
		}
	}


	// Fix planes spawning in places where they crash easily
	{
		using namespace FindPlaneCreationCoorsFix;

		InterceptCall(0x6CD2B8, orgCheckCameraCollisionBuildings, CheckCameraCollisionBuildings_FixParams);
	}


	// Allow hovering on the Jetpack with Keyboard + Mouse controls
	// Does not modify any other controls, only hovering
	{
		using namespace JetpackKeyboardControlsHover;

		ProcessControlInput_DontHover = (void*)0x67ED33;
		ProcessControlInput_Hover = (void*)0x67EDAF;

		Nop(0x67ED2D, 1);
		InjectHook(0x67ED2D + 1, &ProcessControlInput_HoverWithKeyboard, HookType::Jump);
		ReadCall(0x67EDA6, orgGetLookBehindForCar);
	}


	// During riots, don't target the player group during missions
	// Fixes recruited homies panicking during Los Desperados and other riot-time missions
	if (RiotDontTargetPlayerGroupDuringMissions::HasGameBindings())
	{
		using namespace RiotDontTargetPlayerGroupDuringMissions;

		DontSkipTargetting = (void*)0x6CD54C;
		SkipTargetting = (void*)0x6CD7F4;
		InjectHook(0x6CD545, CheckIfInPlayerGroupAndOnAMission, HookType::Jump);
	}


	// Rescale light switching randomness in CVehicle::GetVehicleLightsStatus for PC the randomness range
	// The original randomness was 50000 out of 65535, which is impossible to hit with PC's 32767 range
	{
		static const float LightStatusRandomnessThreshold = 1.0f / 25000.0f;
		Patch<const void*>(0x6D5612 + 2, &LightStatusRandomnessThreshold);
	}

	// Fix damage state of vehicle rear lights both using RIGHT_TAIL_LIGHT
	Patch<uint8_t>(0x6E1D4E + 1, 2); // LEFT_TAIL_LIGHT

	// Fixed vehicles exploding twice if the driver leaves the car while it's exploding
	{
		using namespace RemoveDriverStatusFix;

		Nop(0x6D1955, 2);
		InjectHook(0x6D1955 + 2, RemoveDriver_SetStatus, HookType::Call);

		InterceptCall(0x64C8CE, orgPrepareVehicleForPedExit, PrepareVehicleForPedExit_WreckedCheck);

		// CVehicle::RemoveDriver already sets the status to STATUS_ABANDONED, these are redundant
		Nop(0x48628D, 3);
		Nop(0x647E21, 3);
	}


	// Fixed falling stars rendering black
	{
		using namespace ShootingStarsFix;

		InterceptCall(0x714610, orgRwIm3DTransform, RwIm3DTransform_UnsetTexture);
	}


	// Fix the logic behind exploding cars losing wheels
	// Right now, they lose one wheel at random according to the damage manager, but they always lose the front left wheel visually.
	// This change matches the visuals to the physics
	// Also make it possible for the rear right wheel to be randomly picked
	{
		std::array<uint32_t, 4> spawnFlyingComponent = { 0x6B38CA, 0x6B3CCB, 0x6C6EBE, 0x6CCEF9 };
		CAutomobile::HookEach_SpawnFlyingComponent(spawnFlyingComponent, InterceptCall);

		Nop(0x6B38E4, 5);
		Nop(0x6B3CF1, 5);
		Nop(0x6C6ED8, 5);
		Nop(0x6CCF17, 5);

		static const float fRandomness = -4.0f;
		Patch(0x6C25F5 + 2, &fRandomness);
	}


	// Make script randomness 16-bit, like on PS2
	{
		using namespace Rand16bit;

		std::array<uintptr_t, 2> rands = { 0x4674FE, 0x467533 };

		HookEach_Rand(rands, InterceptCall);
	}


	// Invert a CPed::IsAlive check in CTaskComplexEnterCar::CreateNextSubTask to avoid assigning
	// CTaskComplexLeaveCarAndDie to alive drivers
	// Fixes a bug where stealing the car from the passenger side while holding throttle and/or brake would kill the driver,
	// or briefly resurrect them if they were already dead
	Patch<uint8_t>(0x63F576, 0x75);


	// Improved resolution selection dialog
	if (NewResolutionSelectionDialog::HasGameBindings())
	{
		using namespace NewResolutionSelectionDialog;

		ppRWD3D9 = *AddressByRegion_10<IDirect3D9***>(0x7F6312 + 1);
		FrontEndMenuManager = *(void**)(0x4054DB + 1);

		orgGetDocumentsPath = AddressByRegion_10<char*(*)()>(0x744FB0);

		Patch(AddressByRegion_10(0x746241 + 2), &pDialogBoxParamA_New);
		Patch(AddressByRegion_10(0x745DB3 + 2), &pSetFocus_NOP);

		InterceptCall(AddressByRegion_10(0x7461D8), orgRwEngineGetSubSystemInfo, RwEngineGetSubSystemInfo_GetFriendlyNames);
		InterceptCall(AddressByRegion_10(0x7461ED), orgRwEngineGetCurrentSubSystem, RwEngineGetCurrentSubSystem_FromSettings);
	}


	// Fix some big messages staying on screen longer at high resolutions due to a cut sliding text feature
	// Also since we're touching it, optionally allow to re-enable this feature.
	{
		using namespace SlidingTextsScalingFixes;

		// "Unscale" text sliding thresholds, so texts don't stay on screen longer at high resolutions
		Patch(0x58D2E9 + 1, &FIXED_RES_WIDTH_SCALE);

		// Replace dword ptr [esp+0Ch+X], eax \ dword ptr [esp+0Ch+X]
		// with a constant fld [620.0]
		static const float f620 = FIXED_RES_WIDTH_SCALE - 20.0f;
		Patch(0x58C90E, { 0x90, 0x90, 0xD9, 0x05 });
		Patch(0x58C90E + 4, &f620);
	}


	// Fix post effects not scaling correctly
	// Heat haze not rescaling after changing resolution
	// Water ripple effect having too high wave frequency at higher resolutions
	{
		using namespace PostEffectsScalingFixes;

		std::array<uintptr_t, 4> setCurrentVideoMode = { 0x574509, 0x57D096, 0x57D16E, 0x57D2B2 };

		if (*(uint8_t*)0x701450 == 0xA1)
		{
			pHeatHazeFXTypeLast = *(int32_t**)(0x701450 + 1);
		}
		else
		{
			// Someone re-routed CPostEffects::HeatHazeFXInit and we can't read the variable, fall back to the 1.0 US address
			pHeatHazeFXTypeLast = (int32_t*)0x8D50E4;
		}

		HookEach_SetCurrentVideoMode(setCurrentVideoMode, InterceptCall);
		InterceptCall(0x745C7D, orgSetupBackBufferVertex, SetupBackBufferVertex_Nop);

		if (HasGameBindings_UnderWaterRipple())
		{
			InterceptCall(0x70529C, orgUnderWaterRipple, UnderWaterRipple_ScaleFrequency);
		}
	}


	// Fix heat seeking and gamepad crosshairs not scaling to resolution
	if (CrosshairScalingFixes::HasGameBindings())
	{
		using namespace CrosshairScalingFixes;

		std::array<uintptr_t, 6> renderRotateAspect = {
			// Heat seeking missile crosshair
			0x742EAF, 0x742F45, 0x743073, 0x74311D,

			// Co-op in-car crosshair
			0x743A0A, 0x743BD4,
		};

		// Triangular gamepad crosshairs - their size needs to scale to screen *height*
		std::array<float**, 13> triangleSizes = {
			// Co-op offscreen crosshair
			(float**)(0x7436F1 + 2), (float**)(0x7436FF + 2), (float**)(0x74370D + 2), (float**)(0x74374B + 2),
			(float**)(0x743797 + 2), (float**)(0x7437D0 + 2), (float**)(0x7437FB + 2), (float**)(0x743819 + 2),
			(float**)(0x74386F + 2),

			// Regular crosshair
			(float**)(0x743212 + 2), (float**)(0x74321E + 2), (float**)(0x743259 + 2), (float**)(0x743266 + 2),
		};

		HookEach_RenderOneXLUSprite_Rotate_Aspect(renderRotateAspect, InterceptCall);

		InterceptCall(0x74318D, orgCalcScreenCoors, CalcScreenCoors_Recalculate<triangleSizes.size(), 0>);
		HookEach_GamepadCrosshair(triangleSizes, InterceptMemDisplacement);
	}


	// Fix nitrous recharging faster when reversing the car
	// By Wesser
	{
		using namespace NitrousReverseRechargeFix;

		Nop(0x6A407B, 1);
		InjectHook(0x6A407B + 1, &NitrousControl_DontRechargeWhenReversing, HookType::Call);
	}


	// Fix Hydra's jet thrusters not displaying due to an uninitialized variable in RwMatrix
	// By B1ack_Wh1te
	{
		using namespace JetThrustersFix;

		std::array<uintptr_t, 4> matrixMult = { 0x6CA09F, 0x6CA122, 0x6CA1B2, 0x6CA242 };
		HookEach_MatrixMultiply(matrixMult, InterceptCall);
	}


	// Fix Skimmer not spawning correctly (and shooting up the sky) on Windows 11 24H2
	// Missing vehicles.ide values should have always caused issues, but only in 24H2 fgets/LeaveCriticalSection uses enough stack
	// to scramble the stale values in CFileLoader::LoadVehicleObject.
	{
		using namespace SkimmerVehiclesIdeFix;

		InterceptCall(0x5B6FC7, orgSscanf, sscanf_Defaults);
	}


	// Fix the rear van doors not being properly special cased in CCarEnterExit::GetPositionToOpenCarDoor
	// By B1ack_Wh1te
	Patch<int8_t>(0x64E77B + 6, 13); // ANIM_VEH_VAN - ANIM_VEH_STD


	// Fix jumping on bikes from the front misplacing CJ on the Z axis
	// By B1ack_Wh1te
	Patch(0x64FFAD, { 0xD8, 0x64, 0x24 }); // fsub


	// Fixed most line wraps not scaling to resolution
	// Shared namespace, but separate patch applications per-function
	if (FixedLineWraps::HasGameBindings())
	{
		using namespace FixedLineWraps;

		// CMenuManager::PrintMap
		{
			std::array<uintptr_t, 1> full_width = { 0x575E72 };
			std::array<uintptr_t, 2> right_align = { 0x5760BA, 0x5762C5 };
			std::array<uintptr_t, 2> left_align = { 0x5760C4, 0x5762CF };

			MenuManager::HookEach_PrintMap_FullWidth(full_width, InterceptCall);
			MenuManager::HookEach_PrintMap_Right(right_align, InterceptCall);
			MenuManager::HookEach_PrintMap_Left(left_align, InterceptCall);
		}

		// CMenuManager::DrawStandardMenus
		{
			std::array<uintptr_t, 3> right_align = { 0x5794E6, 0x5796CE, 0x579894 };
			std::array<uintptr_t, 2> left_align = { 0x5794F0, 0x57989E };

			MenuManager::HookEach_DrawStandardMenus_Right(right_align, InterceptCall);
			MenuManager::HookEach_DrawStandardMenus_Left(left_align, InterceptCall);
		}

		// CReplay::Display
		{
			std::array<uintptr_t, 1> set_centre_size = { 0x45C28F };

			Replay::HookEach_Display_Right(set_centre_size, InterceptCall);
		}
	}


	// Stop cops holding one handed guns like gangsters, with a tilted stance
	// Instead, give this behavior to dealers and criminals
	// By iFarbod
	Patch<int8_t>(0x61E52E + 2, 0x11); // PED_TYPE_DEALER
	Patch<int8_t>(0x61E533 + 2, 0x14); // PED_TYPE_CRIMINAL

	Patch<int8_t>(0x625434 + 2, 0x11); // PED_TYPE_DEALER
	Patch<int8_t>(0x625439 + 2, 0x14); // PED_TYPE_CRIMINAL


	// Fix CJ clones spawning in gang roadblocks in neutral zones
	Patch(0x4613C2, { 0x39, 0x5C }); // cmp X, ebx
	Patch<uint8_t>(0x4613C6, 0x7F); // jg X


	// Corona flares not scaling to resolution
	if (CoronaFlaresScaling::HasGameBindings())
	{
		using namespace CoronaFlaresScaling;

		std::array<uintptr_t, 3> render_buffered_flare_sprite = { 0x6FB461, 0x6FB561, 0x6FB5EA };
		HookEach_RenderOneSprite(render_buffered_flare_sprite, InterceptCall);
	}


	// Fix the savegame loading not loading the number of tags correctly
	{
		void* loadData;
		ReadCall(0x5D3DC4, loadData);
		InjectHook(0x5D3DA8, loadData);
	}


	// Fix CREATE_BIRDS mis-interpreting coordinate arguments as integers
	// fild -> fld
	Patch(0x471967, { 0xD9, 0x05 });
	Patch(0x471979, { 0xD9, 0x05 });
	Patch(0x47198E, { 0xD9, 0x05 });
	Patch(0x4719A0, { 0xD9, 0x05 });
	Patch(0x4719B3, { 0xD9, 0x05 });
	Patch(0x4719C7, { 0xD9, 0x05 });


	// Fake the VRAM poll, as it's used to limit resolutions for no reason
	// Instead, assume that all polled resolutions can be used
	InjectHook(0x7455E0, GetAvailableMemory_Fake, HookType::Jump);


	// Display a fallback string if the resolution string is absent
	// This mirrors a 1.01 fix for Advanced Display Settings crashing with 32MB VRAM
	{
		using namespace AdvancedDisplaySettingsCrashFix;
		InterceptCall(0x57A071, orgAsciiToGxtChar, AsciiToGxtChar_NullCheck);
	}


	// Fix 2 player blips both drawing in P1's position
	{
		// nop \ push esi
		Patch(0x588581, { 0x90, 0x56 });

		// mov edx, dword ptr CWorld__Players.pPed[eax]
		Patch(0x56E2A4, { 0x8B, 0x90 });

		// mov ecx, dword ptr CWorld__Players.pPed[eax]
		Patch(0x56E2CB, { 0x8B, 0x88 });
	}


	// Stop gang wars clearing the friendly entity blips when the gang war ends
	// DRM-obfuscated, so exceptionally use patterns
	if (AttackWaveFriendlyBlipsFix::HasGameBindings()) try
	{
		using namespace AttackWaveFriendlyBlipsFix;
		using namespace hook::txn;

		uintptr_t ReleasePedsInAttackWave_start = ModCompat::Utils::GetFunctionAddrIfRerouted(0x445C30);

		auto clear_blip_on_death_check = pattern({{ ReleasePedsInAttackWave_start, ReleasePedsInAttackWave_start + 0x200 }}, "8B 86 74 04 00 00 F6 C4 20").get_one();

		// mov ecx, esi
		// call ClearBlipsOnDeath_AndNotIsInPlayersGroup
		// test al, al
		Patch(clear_blip_on_death_check.get<void>(0), { 0x8B, 0xCE });
		InjectHook(clear_blip_on_death_check.get<void>(2), ClearBlipsOnDeath_AndNotIsInPlayersGroup, HookType::Call);
		Patch(clear_blip_on_death_check.get<void>(7), { 0x84, 0xC0 });
	}
	TXN_CATCH();


	// Speech system fixes
	{
		using namespace SpeechSystemFixes;

		ConversationTopic = *(uint32_t**)(0x43BE8A + 1);

		Patch<int8_t>(0x43B341 + 1, 8); // Let AE_CONV_WEATHER be picked by peds
		InterceptCall(0x43BE4A, orgPedSay_Weather, PedSay_NegativeWeatherOverride); // Add a special case for a positive reply to a negative weather comment

		// Special case CJ's weather responses to fall back to the CD mood instead of CR
		std::array<intptr_t, 1> get_sound_and_bank_ids = {
			0x4E67E2
		};
		HookEach_GetSoundAndBankIDs(get_sound_and_bank_ids, InterceptCall);
		InterceptCall(0x4E5A67, orgGetCurrentCJMood, GetCurrentCJMood_Override);

		Patch<uint8_t>(0x4B9BB9 + 1, 0); // Stop criminals screaming when they run away from cops

		// Account for typos in voices of WMYSGRD and BMYPIMP
		std::array<intptr_t, 2> get_voices = {
			0x5B75AB, 0x5B75BD
		};
		HookEach_GetVoice(get_voices, InterceptCall);

		// Try to fixup VOICE_GEN_S*FYST vs VOICE_GEN_S*FYSTR
		InterceptCall(0x5B749D, orgSscanf_PedsIde, sscanf_PedsIde_FixupVoiceNames);

		// Play turf takeover cheers on CJ, not on his gang members
		Patch(0x444059, { 0x89, 0x04, 0x24, 0xEB, 0x18 }); // mov [esp], eax / jmp 0x444076
		Nop(0x444078, 2);

		// Use the text label, not the info label, for determining what context to play
		std::array<intptr_t, 12> stricmp_calls = {
			0x44408B, 0x4440BC, 0x4440ED, 0x44411D,
			0x44414E, 0x44417F, 0x4441AF, 0x4441E0,
			0x444211, 0x444241, 0x444272, 0x4442A3
		};
		HookEach_TextLabelStricmp(stricmp_calls, InterceptCall);

		// Change CJ's mood to WR after playing the solicit line, not before, so non-WR lines end up getting used
		InterceptCall(0x666A82, orgSetCJMood, SetCJMood_Store);
		InterceptCall(0x666A9C, orgPedSay_Solicit, PedSay_Solicit_ChangeMood);

		// Add missing LSV taunt speech to CTaskComplexGangLeader::DoGangAbuseSpeech
		if (HasGameBindings_DoGangAbuseSpeech())
		{
			std::array<intptr_t, 2> do_gang_abuse_speech = {
				0x660248, 0x66069E
			};
			HookEach_DoGangAbuseSpeech(do_gang_abuse_speech, InterceptCall);
		}

		// Add a missing ATTACK_PLAYER trigger to CTaskComplexGangLeader::DoGangAttackSpeech
		// (it's checking for the player, but doesn't play anything)
		if (HasGameBindings_DoGangAttackSpeech())
		{
			InterceptCall(0x626A5B, orgDoGangAttackSpeech, DoGangAttackSpeech_AddPlayer);
		}

		// Add a CRASH_GENERIC fallback, as not everyone has CRASH_CAR or CRASH_BIKE
		// For example, CJ has no CRASH_BIKE lines, so he's never commenting on crashes when on a bike
		std::array<intptr_t, 2> vehicle_damage_fallback = {
			0x6A8254, 0x6B9145
		};
		HookEach_VehicleDamageFallback(vehicle_damage_fallback, InterceptCall);

		// Re-enable PAIN_SPRAYED
		if (HasGameBindings_PainSprayed())
		{
			CheckForSprayHook_JumpBack = (void*)(0x4B3349 + 9);
			InjectHook(0x4B3349, &CheckForSprayHook_OldBinaries, HookType::Jump);
		}

		// Play CJ's CHASED line from CTaskComplexArrestPed too, so 1-star quotes are not ultra rare
		InterceptCall(0x68D82F, orgPedSay_ArrestPed, PedSay_ArrestPedHook);

		// Fix CJ not playing SHOOT lines when shooting on a mouse and keyboard
		// mov [s_targetEntity], ebp
		Patch(0x7427EF, { 0x90u, 0x89u, 0x2Du });
		WriteMemDisplacement(0x7427EF + 3, &s_targetEntity);
		Nop(0x7427FC, 2);
		InterceptCall(0x74281A, orgPedSay_ShootContext, PedSay_ShootContext_CheckTarget);

		// Enable SURROUNDED and COVER ME lines for cops
		ReadCall(0x62740B, orgFindPlayerWanted_GetPedsGroup);

		std::array<intptr_t, 4> get_peds_group = {
			0x627587, 0x62C96C, 0x62D21B, 0x62CF5A,
		};
		HookEach_GetPedGroupsForCops(get_peds_group, InterceptCall);

		// Enable WHERE_YOU_FROM_POS_REPLY as a reply to WHERE_YOU_FROM_POS
		WriteOffsetValue(0x43BC13 + 2, 0x43BECD);
		WriteOffsetValue(0x43BE14 + 1, 0x43BECD);
		InterceptCall(0x43C0C7, orgPedSay_DismissReply, PedSay_DismissReply_WhereYouFrom);

		// Play DRUGGED_IGNORE on the homie, not on CJ, when they're refusing to be recruited
		InterceptCall(0x60C87A, orgPedSay_MakeThisPedJoinOurGroup_DruggedIgnore, PedSay_MakeThisPedJoinOurGroup_DruggedIgnore);

		// Don't let dead and dying peds start conversations with CJ
		InterceptCall(0x43B213, orgPedIsReadyForConversation, PedIsReadyForConversation_CheckAlive);
	}


	// Good Citizen Bonus
	if (GoodCitizenBonus::HasGameBindings())
	{
		using namespace GoodCitizenBonus;

		// Replace a legacy flag check with a fleeing event check, and play GOOD_CITIZEN on the chasing cop
		Patch<uint8_t>(0x532092, 0x57); // push edi
		InjectHook(0x532093, IsPedFleeingFromCops_PlayAudio, HookType::Call);
		Patch(0x532098, { 0x84, 0xC0, 0x90, 0x90 }); // tesl al, al / nop

		// Prevent the player from getting punished for defending against the criminal they stopped
		Patch<uint8_t>(0x53209E, 0x57); // push edi
		InjectHook(0x53209F, IsNotDying_AndBonusNotGiven, HookType::Call);
		Patch(0x5320A4, { 0x84, 0xC0, 0x90 }); // tesl al, al / nop
		Nop(0x5320B0, 6);

		// Inject a fallback text as GOODBOY doesn't exist by default
		InterceptCall(0x5320C7, orgGetText_Goodboy, GetText_Goodboy_Fallback);
	}


	// Fix CShadows::CastShadowEntityXY ignoring the Up rotation of an object
	{
		using namespace CastShadowEntityFix;

		// lea edx, [esi-4]
		// mov ecx, edi
		Patch(0x709821, { 0x8D, 0x56, 0xFC, 0x8B, 0xCF });
		InjectHook(0x709821 + 5, TransformShadowPoint3D, HookType::Call);
		InjectHook(0x709821 + 10, 0x7098C3, HookType::Jump);

		Nop(0x7098C7, 3);
		Nop(0x7098D2, 3);
	}


	// Fix script draws affecting the line wrapping of a radio station name display
	if (RadioStationDisplayWidth::HasGameBindings())
	{
		using namespace RadioStationDisplayWidth;

		SetCentreSize = reinterpret_cast<decltype(SetCentreSize)>(0x7194E0);
		InterceptCall(0x4E9F7A, orgSetDropColor, SetDropColor_AndCentreSize);
	}


	// Revert leftover GTA III code making Securicars very fragile against the player
	if (MemEquals(0x6A806F, { 0x75 }))
	{
		Patch<uint8_t>(0x6A806F, 0xEB);
	}


	// Intercept binary IPL car generator spawns to fix a Beagle spawn in Fort Carson
	{
		using namespace FortCarsonBeagle;

		InterceptCall(0x406267, orgLoadCarGenerator, LoadCarGenerator_FixupBeagle);
	}

	// Reorder the law enforcement checks in CRoadBlocks::GenerateRoadBlocks to match the other call sites
	// Fixes an issue where road blocks in 'Decoy' (in GTA III) have Barracks and not the Enforcer
	{
		void* are_army_required;
		void* are_swat_required;
		ReadCall(0x461BA7, are_army_required);
		ReadCall(0x461BDD, are_swat_required);

		int32_t army_model_id, swat_model_id;
		Read(0x461BB0 + 1, army_model_id);
		Read(0x461BE6 + 1, swat_model_id);

		InjectHook(0x461BA7, are_swat_required);
		InjectHook(0x461BDD, are_army_required);

		Patch(0x461BB0 + 1, swat_model_id);
		Patch(0x461BE6 + 1, army_model_id);
	}

	// Swap the GANGS and CRIMES section names when exporting stats to stats.html
	// Stat section 3 holds crime stats and section 4 holds gang stats, but the exporter
	// labels them the other way around
	{
		const char* gangsHeader;
		const char* crimesHeader;
		Read(0x57E023 + 1, gangsHeader);
		Read(0x57E02C + 1, crimesHeader);

		Patch(0x57E023 + 1, crimesHeader);
		Patch(0x57E02C + 1, gangsHeader);
	}


	// Allow to store trains in garages
	if (TrainsInGarages::HasGameBindings())
	{
		using namespace TrainsInGarages;
		InterceptCall(0x4480D0, orgCAutomobileCtor, CAutomobileCtor_OrTrain);
	}
}


BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	UNREFERENCED_PARAMETER(lpvReserved);

	if ( fdwReason == DLL_PROCESS_ATTACH )
	{
		const HINSTANCE hInstance = GetModuleHandle(nullptr);
		auto Protect = ScopedUnprotect::SectionOrFullModule(hInstance, ".text");
		auto Protect2 = ScopedUnprotect::Section(hInstance, ".rdata");

		// Only GTA: San Andreas 1.0 (US/EU) is supported. Any other executable is left untouched
		if ( Memory::GetVersion().version == 0 )
		{
			Patch_SA_10(hInstance);
			Memory::FlushCodeChanges();
		}
	}
	return TRUE;
}

extern "C" __declspec(dllexport)
uint32_t GetBuildNumber()
{
	return (SILENTPATCH_REVISION_ID << 8) | SILENTPATCH_BUILD_ID;
}
