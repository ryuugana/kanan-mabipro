/*#include "Patcher_FarDiceThrow.h"
#include "../Addr.h"
#include "../FileSystem.h"

//-----------------------------------------------------------------------------
// Static variable initialization
float CPatcher_FarDiceThrow::throwRadius = 30000.0;
LPBYTE CPatcher_FarDiceThrow::addrTargetReturn = NULL;
//-----------------------------------------------------------------------------
// Constructor

CPatcher_FarDiceThrow::CPatcher_FarDiceThrow(void)
{
	patchname = "Far Dice Throw";

	LPBYTE addrGetSkillUsable = CAddr::Addr(CSkillDiceThrowing_GetSkillUsable);
	if (!addrGetSkillUsable) {
		WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
		WriteLog("   Missing dependency.\n");
		return;
	}

	vector<WORD> patch;
	vector<WORD> backup;

	backup +=
		0xD9, 0x40, 0x44, 0x6A, 0x01, 0xD9, 0x5C, 0x24, 0x18;

	patch +=
		0xE9, 0x01FF, 0x01FF, 0x01FF, 0x01FF, 0x90, 0x90, 0x90, 0x90;

	MemoryPatch mp(addrGetSkillUsable + 0x5B, patch, backup);
	mp.PatchRelativeAddress(0x01, (LPBYTE)SetThrowRadius);
	addrTargetReturn = addrGetSkillUsable + 0x64;

	patches += mp;
	if (CheckPatches())
		WriteLog("Patch initialization successful: %s.\n", patchname.c_str());
	else
		WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
}

//-----------------------------------------------------------------------------
// Hook functions
NAKED void CPatcher_FarDiceThrow::SetThrowRadius(void) {
	__asm {
		fld dword ptr [eax+0x44]
		push 01
		fstp dword ptr [esp+0x18]
		fld throwRadius
		fstp dword ptr[esp+0x18]

		jmp addrTargetReturn
	}
}


//-----------------------------------------------------------------------------
// INI Functions

bool CPatcher_FarDiceThrow::ReadINI(void)
{
	if (ReadINI_Bool(L"FarDiceThrow"))
		return Install();
	return true;
}

bool CPatcher_FarDiceThrow::WriteINI(void)
{
	WriteINI_Bool(L"FarDiceThrow", IsInstalled());
	return true;
}*/