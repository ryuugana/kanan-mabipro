/*#include "Patcher_CombatMasterySwap.h"
#include "../Addr.h"
#include "../FileSystem.h"

//-----------------------------------------------------------------------------
// Static variable initialization
LPBYTE CPatcher_CombatMasterySwap::addrMintWriteWord = NULL;
LPBYTE CPatcher_CombatMasterySwap::addrCMessage = NULL;
LPBYTE CPatcher_CombatMasterySwap::addrComplete = NULL;
LPBYTE CPatcher_CombatMasterySwap::addrTargetReturn = NULL;
LPBYTE CPatcher_CombatMasterySwap::addrTargetReturn1 = NULL;
LPBYTE CPatcher_CombatMasterySwap::addrTargetReturn2 = NULL;
LPBYTE CPatcher_CombatMasterySwap::addrTargetReturn3 = NULL;
LPBYTE CPatcher_CombatMasterySwap::addrTargetReturn4 = NULL;
LPBYTE CPatcher_CombatMasterySwap::addrTargetReturn5 = NULL;
LPBYTE CPatcher_CombatMasterySwap::addrTargetReturn6 = NULL;
LPBYTE CPatcher_CombatMasterySwap::addrTargetReturn7 = NULL;
int CPatcher_CombatMasterySwap::isSkillOn = 0;
int CPatcher_CombatMasterySwap::isCharge = 0;
int CPatcher_CombatMasterySwap::petCheck = 0;
short int CPatcher_CombatMasterySwap::skillCheck = 0;
int CPatcher_CombatMasterySwap::currentSkill = 0;
LPBYTE CPatcher_CombatMasterySwap::oldAddr = 0;
//-----------------------------------------------------------------------------
// Constructor

CPatcher_CombatMasterySwap::CPatcher_CombatMasterySwap(void)
{
	patchname = "CombatMasterySwap";

	addrMintWriteWord = CAddr::Addr(Mint_Write16);
	addrCMessage = CAddr::Addr(Mint_CMessage);
	addrComplete = CAddr::Addr(ISkillMgr_Complete);

	if (!addrMintWriteWord ||
		!addrCMessage ||
		!addrComplete) {
		WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
		WriteLog("   Missing dependency.\n");
		return;
	}

	vector<WORD> patch;
	vector<WORD> backup;

	backup +=
		0x68, 0x00, 0x7D, 0x00, 0x00;

	patch +=
		0xE9, 0x01FF, 0x01FF, 0x01FF, 0x01FF;	// NOP

	MemoryPatch mp((LPBYTE)0x63854209, patch, backup);
	mp.PatchRelativeAddress(0x01, (LPBYTE)patchCombatMastery);
	addrTargetReturn = (LPBYTE)0x6385420E;
	addrTargetReturn1 = (LPBYTE)0x6385422A;

	vector<WORD> patch1;
	vector<WORD> backup1;

	backup1 +=
		0x68, 0x89, 0x69, 0x00, 0x00;

	patch1 +=
		0xE9, 0x01FF, 0x01FF, 0x01FF, 0x01FF;

	MemoryPatch mp1((LPBYTE)0x63A264E7, patch1, backup1);
	mp1.PatchRelativeAddress(0x01, (LPBYTE)cancelSkill);
	addrTargetReturn2 = (LPBYTE)0x63A264EC;

	vector<WORD> patch2;
	vector<WORD> backup2;

	backup2 +=
		0x68, 0x87, 0x69, 0x00, 0x00;

	patch2 +=
		0xE9, 0x01FF, 0x01FF, 0x01FF, 0x01FF;

	MemoryPatch mp2((LPBYTE)addrComplete+0x1C, patch2, backup2);
	mp2.PatchRelativeAddress(0x01, (LPBYTE)completeSkill);
	addrTargetReturn3 = (LPBYTE)addrComplete+0x21;

	vector<WORD> patch3;
	vector<WORD> backup3;

	backup3 +=
		0x68, 0x86, 0x69, 0x00, 0x00;

	patch3 +=
		0xE9, 0x01FF, 0x01FF, 0x01FF, 0x01FF;

	MemoryPatch mp3((LPBYTE)0x63A262F8, patch3, backup3);
	mp3.PatchRelativeAddress(0x01, (LPBYTE)cancelSkill1);
	addrTargetReturn4 = (LPBYTE)0x63A262FD;

	vector<WORD> patch4;
	vector<WORD> backup4;

	backup4 +=
		0x68, 0x82, 0x69, 0x00, 0x00;

	patch4 +=
		0xE9, 0x01FF, 0x01FF, 0x01FF, 0x01FF;

	MemoryPatch mp4((LPBYTE)0x63A26060, patch4, backup4);
	mp4.PatchRelativeAddress(0x01, (LPBYTE)setIsSkillOn);
	addrTargetReturn5 = (LPBYTE)0x63A26065;

	// CHARGE FIX

	vector<WORD> patch5;
	vector<WORD> backup5;

	backup5 +=
		0x0F, 0xB7, 0xF8, 0x33, 0xC0;

	patch5 +=
		0xE9, 0x01FF, 0x01FF, 0x01FF, 0x01FF;

	MemoryPatch mp5((LPBYTE)0x63A2630B, patch5, backup5);
	mp5.PatchRelativeAddress(0x01, (LPBYTE)fixCharge);
	addrTargetReturn6 = (LPBYTE)0x63A26310;

	vector<WORD> patch6;
	vector<WORD> backup6;

	backup6 +=
		0x68, 0x00, 0x7D, 0x00, 0x00;

	patch6 +=
		0xE9, 0x01FF, 0x01FF, 0x01FF, 0x01FF;	// NOP

	MemoryPatch mp6((LPBYTE)0x63854209, patch6, backup6);
	mp6.PatchRelativeAddress(0x01, (LPBYTE)patchCombatMastery);
	addrTargetReturn = (LPBYTE)0x6385420E;
	addrTargetReturn1 = (LPBYTE)0x6385422A;

	patches += mp;
	patches += mp1;
	patches += mp2;
	patches += mp3;
	patches += mp4;
	patches += mp5;
	if (CheckPatches())
		WriteLog("Patch initialization successful: %s.\n", patchname.c_str());
	else
		WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
}

//-----------------------------------------------------------------------------
// Hook functions
NAKED void CPatcher_CombatMasterySwap::patchCombatMastery(void)
{
	__asm {
		mov		petCheck, edx
	}

	if (petCheck == 0x100100) {
		__asm {
			push	0x7D00
			jmp		addrTargetReturn
		}
	}
	else if (isSkillOn || isCharge) {
		isCharge = 0;
		__asm {
			push	0x7D00
			jmp		addrTargetReturn
		}
	}
	else {
		__asm {
			push	0x6982
			lea		ecx, [ebp - 0x30]
			nop
			call	addrCMessage
			push	[ebp + 0x0C]
			and		dword ptr[ebp - 0x04], 0x0
			push	[ebp + 0x08]
			lea		ecx, [ebp - 0x30]
			nop
			mov		oldAddr, eax
			mov		eax, currentSkill
			mov		dword ptr[esp], eax
			mov		eax, oldAddr
			call	addrMintWriteWord
			jmp		addrTargetReturn1
		}
	}
}

NAKED void CPatcher_CombatMasterySwap::cancelSkill(void)
{
	__asm {
		mov isSkillOn, 0
		push 0x6989
		jmp addrTargetReturn2
	}
}

NAKED void CPatcher_CombatMasterySwap::completeSkill(void)
{
	__asm {
		mov isSkillOn, 0
		push 0x6987
		jmp addrTargetReturn3
	}
}

NAKED void CPatcher_CombatMasterySwap::cancelSkill1(void)
{
	__asm {
		mov isSkillOn, 0
		push 0x6986
		jmp addrTargetReturn4
	}
}

NAKED void CPatcher_CombatMasterySwap::setIsSkillOn(void)
{
	__asm {
		mov isSkillOn, 1
		push 0x6982
		jmp addrTargetReturn5
	}
}

NAKED void CPatcher_CombatMasterySwap::fixCharge(void)
{
	__asm {
		mov skillCheck, ax
	}

	if (skillCheck == 0x4E2B) {
		isCharge = 1;
	}

	__asm {
		movzx	edi, ax
		xor		eax, eax
		jmp		addrTargetReturn6
	}
}

//-----------------------------------------------------------------------------
// INI Functions

bool CPatcher_CombatMasterySwap::ReadINI(void)
{
	isSkillOn = 0;
	isCharge = 0;
	skillCheck = 0;
	currentSkill = ReadINI_Int(L"CombatMasterySwap", 0, 9999999);
	if (currentSkill)
		return Install();
	else
		return Uninstall();
	return true;
}

bool CPatcher_CombatMasterySwap::WriteINI(void)
{
	WriteINI_Int(L"CombatMasterySwap", currentSkill);
	return true;
}*/