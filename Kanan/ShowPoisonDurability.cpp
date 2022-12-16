/*#include "Patcher_ShowPoisonDurability.h"
#include "../Addr.h"
#include "../FileSystem.h"

//-----------------------------------------------------------------------------
// Static variable initialization
wchar_t CPatcher_ShowPoisonDurability::dataStr[255] = L"</color>";
LPBYTE CPatcher_ShowPoisonDurability::addrCallClear = NULL;
LPBYTE CPatcher_ShowPoisonDurability::addrTargetReturn1 = NULL;
LPBYTE CPatcher_ShowPoisonDurability::addrTargetReturn2 = NULL;
LPBYTE CPatcher_ShowPoisonDurability::addrIsPoisoned = NULL;
LPBYTE CPatcher_ShowPoisonDurability::addrGetItemBasePoison = NULL;
LPBYTE CPatcher_ShowPoisonDurability::addrGetItemPotionPoison = NULL;
LPBYTE CPatcher_ShowPoisonDurability::addrGetItemPotionPoisonDurability = NULL;
int CPatcher_ShowPoisonDurability::poisonDurability = 0;
//-----------------------------------------------------------------------------
// Constructor

CPatcher_ShowPoisonDurability::CPatcher_ShowPoisonDurability(void)
{
	patchname = "Show Poison Durability";

	addrIsPoisoned = CAddr::Addr(IItem_IsPoisoned);
	addrGetItemBasePoison = CAddr::Addr(IItem_GetItemBasePoison);
	addrGetItemPotionPoison = CAddr::Addr(IItem_GetItemPotionPoison);
	addrGetItemPotionPoisonDurability = CAddr::Addr(IItem_GetItemPotionPoisonDurability);

	if (!addrIsPoisoned ||
		!addrGetItemBasePoison ||
		!addrGetItemPotionPoison ||
		!addrGetItemPotionPoisonDurability) {
		WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
		WriteLog("   Missing dependency.\n");
		return;
	}

	vector<WORD> patch;
	vector<WORD> backup;

	backup +=
		0xE8, 0xE3, 0xFF, 0xFF, 0xFF;

	patch +=
		0xE9, 0x01FF, 0x01FF, 0x01FF, 0x01FF;

	MemoryPatch mp(addrIsPoisoned, patch, backup);
	mp.PatchRelativeAddress(0x01, (LPBYTE)GetItemPoison);

	addrTargetReturn1 = addrIsPoisoned + 0x05;

	
	vector<WORD> patch2;
	vector<WORD> backup2;

	backup2 +=
		0x68, 0x20, 0x9F, 0xDC, 0x63;

	patch2 +=
		-1, ((int)(&dataStr) & 0xFF),
		(((int)(&dataStr) & 0xFF00) >> 8),
		(((int)(&dataStr) & 0xFF0000) >> 16),
		(((int)(&dataStr) & 0xFF000000) >> 24);

	MemoryPatch mp2((LPBYTE)0x63922E5C, patch2, backup2);

	patches += mp;
	patches += mp2;
	if (CheckPatches())
		WriteLog("Patch initialization successful: %s.\n", patchname.c_str());
	else
		WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
}

//-----------------------------------------------------------------------------
// Hook functions
NAKED void CPatcher_ShowPoisonDurability::GetItemPoison(void) {
	__asm {
		PUSH	esi
		PUSH	edi
		MOV		esi,ecx
		PUSHAD
		CALL	addrGetItemPotionPoisonDurability
		MOV		[poisonDurability], eax
		POPAD
		CALL	addrGetItemPotionPoison
		MOV		ecx,esi
		MOVZX	edi,ax
		CALL	addrGetItemBasePoison
		ADD		eax,edi
		POP		edi
		POP		esi
		PUSHAD
		CALL	patchColorString
		POPAD
		JMP		addrTargetReturn1
	}
}

void __stdcall CPatcher_ShowPoisonDurability::patchColorString() {
	swprintf_s(dataStr, 255, L"\n\nPoison Durability: %d/100</color>", poisonDurability);
}
//-----------------------------------------------------------------------------
// INI Functions

bool CPatcher_ShowPoisonDurability::ReadINI(void)
{
	if (ReadINI_Bool(L"ShowPoisonDurability"))
		return Install();
	return true;
}

bool CPatcher_ShowPoisonDurability::WriteINI(void)
{
	WriteINI_Bool(L"ShowPoisonDurability", IsInstalled());
	return true;
}*/