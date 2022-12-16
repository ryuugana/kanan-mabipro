/*#include "Patcher_ItemSplitQuantity.h"
#include "../Addr.h"
#include "../FileSystem.h"

//-----------------------------------------------------------------------------
// Static variable initialization
int CPatcher_ItemSplitQuantity::itemQuantity = 1;
LPBYTE CPatcher_ItemSplitQuantity::addrTargetReturn = NULL;
//-----------------------------------------------------------------------------
// Constructor

CPatcher_ItemSplitQuantity::CPatcher_ItemSplitQuantity(void)
{
	patchname = "Item Split Quantity";

	vector<WORD> patch;
	vector<WORD> backup;

	backup +=
		0x33, 0xC0, 0x40, 0x8D, 0x4D, 0xA0;

	patch +=
		0xE9, 0x01FF, 0x01FF, 0x01FF, 0x01FF, 0x90;	// NOP => NOP

	MemoryPatch mp( NULL, patch, backup);
	mp.Search(L"Pleione.dll");
	mp.PatchRelativeAddress(0x01, (LPBYTE)ModifyQuantity);
	addrTargetReturn = mp.GetAddr() + 0x06;

	patches += mp;
	if (CheckPatches())
		WriteLog("Patch initialization successful: %s.\n", patchname.c_str());
	else
		WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
}

//-----------------------------------------------------------------------------
// Hook functions
NAKED void CPatcher_ItemSplitQuantity::ModifyQuantity(void) {
	
	_asm {
		xor eax, eax
		inc eax
		lea ecx, [ebp - 60]
		mov eax, itemQuantity
		jmp addrTargetReturn
	}
}

int CPatcher_ItemSplitQuantity::GetQuantity(void)
{
	return itemQuantity;
}

// Options
bool CPatcher_ItemSplitQuantity::SetQuantity(int value) {
	if (patches.empty()) return false;
	itemQuantity = value;
	return true;
}
//-----------------------------------------------------------------------------
// INI Functions

bool CPatcher_ItemSplitQuantity::ReadINI(void)
{
	if (ReadINI_Bool(L"ItemSplitQuantity")) {
		int tempQuantity = ReadINI_Int(L"ItemSplitQuantity", 1, 10);
		if (!SetQuantity(tempQuantity))
			return false;
		return Install();
	}
	return true;
}

bool CPatcher_ItemSplitQuantity::WriteINI(void)
{
	WriteINI_Bool(L"ItemSplitQuantity", IsInstalled());
	return true;
}*/