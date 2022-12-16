/*#include "Patcher_ShowItemID.h"
#include "../Addr.h"
#include "../FileSystem.h"

//-----------------------------------------------------------------------------
// Static variable initialization
int CPatcher_ShowItemID::itemID = NULL;
wchar_t CPatcher_ShowItemID::dataColorString1[] = L"<color=1>";
LPBYTE CPatcher_ShowItemID::addrCommonAPI = NULL;
LPBYTE CPatcher_ShowItemID::addrg_cStandardAPIMgrBlock = NULL;
LPBYTE CPatcher_ShowItemID::addrTargetReturn = NULL;
LPBYTE CPatcher_ShowItemID::addrTargetReturn1 = NULL;
//-----------------------------------------------------------------------------
// Constructor

CPatcher_ShowItemID::CPatcher_ShowItemID(void)
{
	patchname = "Show Item ID";

	// LPBYTE addrFindItem = CAddr::Addr(Stdapi_FindItem);
	LPBYTE addrGetToolTipString = CAddr::Addr(CSetItemMgr_GetToolTipString);
	addrCommonAPI = CAddr::Addr(CStandardAPIMgr_CommonAPI);
	addrg_cStandardAPIMgrBlock = CAddr::Addr(g_cStandardAPIMgrBlock);
	if (!addrGetToolTipString ||
		!addrg_cStandardAPIMgrBlock ||
		!addrCommonAPI) {
		WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
		WriteLog("   Missing dependency.\n");
		return;
	}

	//// patch 1

	//vector<WORD> patch;
	//vector<WORD> backup;

	//backup +=
	//	0xE8, -1, -1, -1, -1;	// NOP

	//patch +=
	//	-1, 0x01FF, 0x01FF, 0x01FF, 0x01FF;	// NOP => NOP

	//MemoryPatch mp(addrGetToolTipString+0x45, patch, backup);
	//mp.PatchRelativeAddress(0x01, (LPBYTE)FindItem);

	// actual patch 1

	vector<WORD> patch;
	vector<WORD> backup;

	backup +=
		0x55,
		0x83, 0xEC, 0x64,
		0x6A, 0x20;

	patch +=
		0xE9, 0x01FF, 0x01FF, 0x01FF, 0x01FF, 0x90;	// NOP => NOP

	MemoryPatch mp((LPBYTE)0x63922962, patch, backup);
	mp.PatchRelativeAddress(0x01, (LPBYTE)GetItemID);
	addrTargetReturn = (LPBYTE)0x63922967;

	// patch 2

	vector<WORD> patch1;
	vector<WORD> backup1;

	backup1 +=
		0x68, 0x48, 0xAD, 0xDC, 0x63;	// NOP

	patch1 +=
		0xE9, 0x01FF, 0x01FF, 0x01FF, 0x01FF;

	MemoryPatch mp1((LPBYTE)0x63922E82, patch1, backup1);
	mp1.PatchRelativeAddress(0x01, (LPBYTE)patchDataColorString);
	addrTargetReturn1 = (LPBYTE)0x63922E87;

	patches += mp;
	patches += mp1;
	if (CheckPatches())
		WriteLog("Patch initialization successful: %s.\n", patchname.c_str());
	else
		WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
}

//-----------------------------------------------------------------------------
// Hook functions
//NAKED void CPatcher_ShowItemID::FindItem(void) {
//	__asm {
//		mov     ecx, addrg_cStandardAPIMgrBlock
//		call	addrCommonAPI
//		push	[esp+0x08]
//		mov		edx,[eax]
//		push	[esp+0x08]
//		mov		ecx,eax
//		call	dword ptr [edx+0x98]
//		mov		edi,[eax+0xC]
//		mov		itemID, edi
//		ret
//	}
//}

NAKED void CPatcher_ShowItemID::GetItemID() {
	__asm {
		mov		eax,[ecx+0xC]
		mov		itemID,eax
		push	ebp
		sub		esp,0x64
		push	0x20
		jmp		addrTargetReturn
	}
}

NAKED void CPatcher_ShowItemID::patchDataColorString() {
	__asm {
		call	injectItemID
		push	offset dataColorString1
		jmp		addrTargetReturn1
	}
}

void __stdcall CPatcher_ShowItemID::injectItemID() {
	swprintf_s(dataColorString1, 255, L"\nItem ID: %d<color=1>", itemID);
}

//-----------------------------------------------------------------------------
// INI Functions

bool CPatcher_ShowItemID::ReadINI(void)
{
	if (ReadINI_Bool(L"ShowItemID"))
		return Install();
	return true;
}

bool CPatcher_ShowItemID::WriteINI(void)
{
	WriteINI_Bool(L"ShowItemID", IsInstalled());
	return true;
}*/