/*#include "Patcher_ModifyFontSize.h"
#include "../Addr.h"
#include "../FileSystem.h"

//-----------------------------------------------------------------------------
// Static variable initialization
int CPatcher_ModifyFontSize::fontSize = 12;
LPBYTE CPatcher_ModifyFontSize::addrTargetReturn = NULL;
//-----------------------------------------------------------------------------
// Constructor

CPatcher_ModifyFontSize::CPatcher_ModifyFontSize(void)
{
	patchname = "ModifyFontSize";

	vector<WORD> patch;
	vector<WORD> backup;

	backup +=
		0xC7, 0x80, 0x9C, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00;

	patch +=
		0xE9, 0x01FF, 0x01FF, 0x01FF, 0x01FF, 0x90, 0x90, 0x90, 0x90, 0x90;

	MemoryPatch mp(NULL, patch, backup);
	mp.Search(L"Pleione.dll");
	mp.PatchRelativeAddress(0x01, (LPBYTE)PatchFontSize);
	addrTargetReturn = mp.GetAddr() + 0xA;

	patches += mp;
	if (CheckPatches())
		WriteLog("Patch initialization successful: %s.\n", patchname.c_str());
	else
		WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
}

//-----------------------------------------------------------------------------
// Hook functions
NAKED void CPatcher_ModifyFontSize::PatchFontSize(void)
{
	__asm {
		push ecx
		mov ecx, fontSize
		mov [eax + 0x0000009C], ecx
		pop ecx
		jmp	addrTargetReturn
	}
}

//-----------------------------------------------------------------------------
// INI Functions

int CPatcher_ModifyFontSize::GetFontSize(void)
{
	return fontSize;
}

bool CPatcher_ModifyFontSize::SetFontSize(int value) {
	if (patches.empty()) return false;
	fontSize = value;
	return true;
}

bool CPatcher_ModifyFontSize::ReadINI(void)
{
	if (ReadINI_Bool(L"ModifyFontSize")) {
		int tempSize = ReadINI_Int(L"ModifyFontSize", 1, 30);
		if (!SetFontSize(tempSize))
			return false;
		return Install();
	}
	return true;
}

bool CPatcher_ModifyFontSize::WriteINI(void)
{
	WriteINI_Int(L"ModifyFontSize", fontSize);
	return true;
}*/