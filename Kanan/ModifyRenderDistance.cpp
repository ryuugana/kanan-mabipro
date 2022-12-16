/*#include "Patcher_ModifyRenderDistance.h"
#include "../Addr.h"
#include "../FileSystem.h"

//-----------------------------------------------------------------------------
// Static variable initialization
float CPatcher_ModifyRenderDistance::renderDistanceLimit = 15000;
LPBYTE CPatcher_ModifyRenderDistance::addrTargetReturn = NULL;
LPBYTE CPatcher_ModifyRenderDistance::addrRenderDistanceLimit = NULL;
//-----------------------------------------------------------------------------
// Constructor

CPatcher_ModifyRenderDistance::CPatcher_ModifyRenderDistance(void)
{
	patchname = "Modify Render Distance";

	LPBYTE addrCameraSetProjectionDesc = CAddr::Addr(Camera_SetProjectionDesc);
	if (!addrCameraSetProjectionDesc) {
		WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
		WriteLog("   Missing dependency.\n");
		return;
	}

	vector<WORD> patch;
	vector<WORD> backup;

	backup +=
		0xD9, 0x45, 0x0C, 0xD9, 0x5E, 0x40;

	patch +=
		0xE9, 0x01FF, 0x01FF, 0x01FF, 0x01FF, 0x90;

	MemoryPatch mp(addrCameraSetProjectionDesc+0x0D, patch, backup);
	mp.PatchRelativeAddress(0x01, (LPBYTE)SetRenderDistance);
	addrTargetReturn = addrCameraSetProjectionDesc + 0x12;
	addrRenderDistanceLimit = (LPBYTE)&renderDistanceLimit;

	patches += mp;
	if (CheckPatches())
		WriteLog("Patch initialization successful: %s.\n", patchname.c_str());
	else
		WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
}

//-----------------------------------------------------------------------------
// Hook functions
NAKED void CPatcher_ModifyRenderDistance::SetRenderDistance(void)
{
	__asm {
		fld		dword ptr[ebp + 0x0C]
		fstp	dword ptr[esi + 0x40]
		fld		renderDistanceLimit
		fstp	dword ptr[esi + 0x40]
		jmp		addrTargetReturn
	}
}

bool CPatcher_ModifyRenderDistance::SetDistance(float newDistance) {
	renderDistanceLimit = newDistance;
	return true;
}

int CPatcher_ModifyRenderDistance::GetRenderDistance() {
	return renderDistanceLimit;
}
//-----------------------------------------------------------------------------
// INI Functions

bool CPatcher_ModifyRenderDistance::ReadINI(void)
{
	if (ReadINI_Bool(L"ModifyRenderDistance"))
	{
		int zoomSetting = ReadINI_Int(L"ModifyRenderDistance", 5000, 100000);
		if (!SetDistance(zoomSetting))
			return false;
		return Install();
	}
	return true;
}

bool CPatcher_ModifyRenderDistance::WriteINI(void)
{
	WriteINI_Int(L"ModifyRenderDistance", GetRenderDistance());
	return true;
}*/