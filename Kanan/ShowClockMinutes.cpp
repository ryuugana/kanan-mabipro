/*#include "Patcher_ShowClockMinutes.h"
#include "../Addr.h"
#include "../FileSystem.h"

//-----------------------------------------------------------------------------
// Static variable initialization

//-----------------------------------------------------------------------------
// Constructor

CPatcher_ShowClockMinutes::CPatcher_ShowClockMinutes(void)
{
	patchname = "Show Clock Minutes";

	// 12 hour clock
	vector<WORD> patch1;
	vector<WORD> backup1;

	backup1 +=
		0xF7, 0xF1, 0x8B, 0x0D, 0x94, 0x05, 0xDA, 0x63, 0x29, 0x55, 0x4C;

	patch1 +=
		0x90, 0x90, -1, -1, -1, -1, -1, -1, -1, -1, -1;

	MemoryPatch mp1(NULL, patch1, backup1);
	mp1.Search(L"Pleione.dll");

	// 24 hour clock
	vector<WORD> patch2;
	vector<WORD> backup2;

	backup2 +=
		0xF7, 0xF1, 0x8B, 0x1D, 0x44, 0x05, 0xDA, 0x63, 0x68, 0x08, 0x42;

	patch2 +=
		0x90, 0x90, -1, -1, -1, -1, -1, -1, -1, -1, -1;

	MemoryPatch mp2(NULL, patch2, backup2);
	mp2.Search(L"Pleione.dll");

	patches += mp1;
	patches += mp2;
	if (CheckPatches())
		WriteLog("Patch initialization successful: %s.\n", patchname.c_str());
	else
		WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
}

//-----------------------------------------------------------------------------
// Hook functions

//-----------------------------------------------------------------------------
// INI Functions

bool CPatcher_ShowClockMinutes::ReadINI(void)
{
	if (ReadINI_Bool(L"ShowClockMinutes"))
		return Install();
	return true;
}

bool CPatcher_ShowClockMinutes::WriteINI(void)
{
	WriteINI_Bool(L"ShowClockMinutes", IsInstalled());
	return true;
}*/