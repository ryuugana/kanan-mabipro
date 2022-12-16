/*#include "Patcher_ShowTrueHP.h"
#include "../Addr.h"
#include "../FileSystem.h"

//-----------------------------------------------------------------------------
// Static variable initialization
float CPatcher_ShowTrueHP::currentLife = 0;
float CPatcher_ShowTrueHP::maxLife = 0;
LPBYTE CPatcher_ShowTrueHP::addrGetLife = NULL;
LPBYTE CPatcher_ShowTrueHP::addrGetLifeMax = NULL;
LPBYTE CPatcher_ShowTrueHP::addrGetLifeMaxBase = NULL;
LPBYTE CPatcher_ShowTrueHP::addrGetInterfaceLifeDamaged = NULL;
LPBYTE CPatcher_ShowTrueHP::addrGetLifePercent = NULL;
LPBYTE CPatcher_ShowTrueHP::addrGetLifeDamaged = NULL;
LPBYTE CPatcher_ShowTrueHP::addrGetLifeReturn = NULL;
LPBYTE CPatcher_ShowTrueHP::addrGetLifeDamagedReturn = NULL;

//-----------------------------------------------------------------------------
// Constructor

CPatcher_ShowTrueHP::CPatcher_ShowTrueHP(void)
{
	patchname = "ShowTrueHP";

	//addrGetLife = CAddr::Addr(IParameterBase2_GetLife);	// CURRENT HP
	addrGetLifeDamaged = CAddr::Addr(IParameterBase2_GetLifeDamaged); // MAX HP
	addrGetLifeMax = CAddr::Addr(IParameterBase2_GetLifeMax); // GETS TRUE MAX HP 
	addrGetLifeMaxBase = CAddr::Addr(IParameterBase2_GetLifeMaxBase);
	addrGetInterfaceLifeDamaged = CAddr::Addr(IParameterBase2_GetInterfaceLifeDamaged);
	addrGetLifePercent = CAddr::Addr(IParameterBase2_GetLifePercent);
	if (!addrGetLifeMax ||
		!addrGetLifeDamaged ||
		!addrGetLifeMaxBase ||
		!addrGetInterfaceLifeDamaged ||
		!addrGetLifePercent) {
		WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
		WriteLog("   Missing dependency.\n");
		return;
	}

	// Obtains value of true CURRENT HP

	/*vector<WORD> patch;
	vector<WORD> backup;

	backup +=
		0xD9, 0x5F, 0x04,
		0x5F,
		0xD9, 0x86, 0x28, 0x01, 0x00, 0x00;

	patch +=
		-1, -1, -1,
		-1,
		0xE9, 0x01FF, 0x01FF, 0x01FF, 0x01FF, 0x90;

	MemoryPatch mp(addrGetLife + 0x54, patch, backup);
	mp.PatchRelativeAddress(0x01, (LPBYTE)GetTrueCurrentLife);
	addrGetLifeReturn = addrGetLife + 0x5E;//////////////////////////////////////////////

	// Inject our own GetLifeMax function
LPBYTE addrTarget = (LPBYTE)0x6397894D;
vector<WORD> patch;
vector<WORD> backup;

backup +=
0xEB, 0x06,
0x90,
0xE8, -1, -1, -1, -1,
0xC2, 0x04, 0x00;

patch +=
-1, -1,
-1,
-1, 0x01FF, 0x01FF, 0x01FF, 0x01FF,
-1, -1, -1;

MemoryPatch mp(addrTarget, patch, backup);
mp.PatchRelativeAddress(0x01, (LPBYTE)GetLifeMax);

// Obtains value of true MAX HP
vector<WORD> patch1;
vector<WORD> backup1;

backup1 +=
0xD9, 0x5F, 0x04,
0x5F,
0xD9, 0x86, 0x40, 0x01, 0x00, 0x00;

patch1 +=
-1, -1, -1,
-1,
0xE9, 0x01FF, 0x01FF, 0x01FF, 0x01FF, 0x90;

MemoryPatch mp1(addrGetLifeDamaged + 0x54, patch1, backup1);
mp1.PatchRelativeAddress(0x01, (LPBYTE)GetTrueMaxLife);
addrGetLifeDamagedReturn = addrGetLifeDamaged + 0x5E;

// Patch GetLifeMax function
vector<WORD> patch2;
vector<WORD> backup2;

backup2 +=
0xE8, -1, -1, -1, -1;

patch2 +=
-1, 0x01FF, 0x01FF, 0x01FF, 0x01FF;

MemoryPatch mp2(addrGetInterfaceLifeDamaged + 0x12, patch2, backup2);
mp2.PatchRelativeAddress(0x01, (LPBYTE)GetLifeMax);

//// Patch GetLifePercent function part 1
//vector<WORD> patch3;
//vector<WORD> backup3;

//backup3 +=
//	0xE8, -1, -1, -1, -1;

//patch3 +=
//	-1, 0x01FF, 0x01FF, 0x01FF, 0x01FF;

//MemoryPatch mp3(addrGetLifePercent + 0x8, patch3, backup3);
//mp3.PatchRelativeAddress(0x01, (LPBYTE)GetLifeMax);

//// Patch GetLifePercent function part 2
//vector<WORD> patch4;
//vector<WORD> backup4;

//backup4 +=
//	0xE8, -1, -1, -1, -1;

//patch4 +=
//	-1, 0x01FF, 0x01FF, 0x01FF, 0x01FF;

//MemoryPatch mp4(addrGetLifePercent + 0x2E, patch4, backup4);
//mp4.PatchRelativeAddress(0x01, (LPBYTE)GetLifeMax);

// patches += mp;
patches += mp1;
patches += mp2;
if (CheckPatches())
WriteLog("Patch initialization successful: %s.\n", patchname.c_str());
else
WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
}

//-----------------------------------------------------------------------------
// Hook functions
//NAKED void CPatcher_ShowTrueHP::GetTrueCurrentLife(void) {
//	__asm {
//		fld		dword ptr[esi + 0x128]
//		fst		currentLife
//		jmp		addrGetLifeReturn
//	}
//}

NAKED void CPatcher_ShowTrueHP::GetLifeMax(void) {
	__asm {
		push	ebp
		mov		ebp, esp
		push	ecx
		push	ecx
		push	esi
		fld		maxLife
		pop		esi
		leave
		ret
	}
}

NAKED void CPatcher_ShowTrueHP::GetTrueMaxLife(void) {
	__asm {
		fld		dword ptr[esi + 0x140]
		fst		maxLife
		jmp		addrGetLifeDamagedReturn
	}
}

//-----------------------------------------------------------------------------
// INI Functions

bool CPatcher_ShowTrueHP::ReadINI(void)
{
	if (ReadINI_Bool(L"ShowTrueHP"))
		return Install();
	return true;
}

bool CPatcher_ShowTrueHP::WriteINI(void)
{
	WriteINI_Bool(L"ShowTrueHP", IsInstalled());
	return true;
}*/