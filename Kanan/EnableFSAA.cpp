/*
#include "Patcher_EnableFSAA.h"
#include "../Addr.h"
#include "../FileSystem.h"

//-----------------------------------------------------------------------------
// Static variable initialization

LPBYTE CPatcher_EnableFSAA::addrReadFSAA = NULL;
LPBYTE CPatcher_EnableFSAA::addrReadFSAAQuality = NULL;
LPBYTE CPatcher_EnableFSAA::addrTargetReturn = NULL;

//-----------------------------------------------------------------------------
// Constructor

CPatcher_EnableFSAA::CPatcher_EnableFSAA( void )
{
	patchname = "Enable FSAA";

	addrReadFSAA = CAddr::Addr(CGameOptionMgr_ReadFSAA);
	addrReadFSAAQuality = CAddr::Addr(CGameOptionMgr_ReadFSAAQuality);

	if (!addrReadFSAA ||
		!addrReadFSAAQuality)
	{
		WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
		WriteLog("   Missing dependency.\n");
		return;
	}

	vector<WORD> patch1;
	vector<WORD> backup1;
	backup1 +=
		0x89, 0x7E, 0x10,						// +3B: MOV [ESI+10h], EDI
		0x89, 0x7E, 0x14,						// +3E: MOV [ESI+14h], EDI
		0xE8, -1, -1, -1, -1,					// +41: CALL pleione::CGameOptionMgr::ReadDithering(void)
		0x8B, 0xCE,								// +46: MOV ECX, ESI
		0x88, 0x46, 0x4C,						// +48: MOV [ESI+4Ch], AL
		0xE8, -1, -1, -1, -1,					// +4B: CALL xxxxxxxx
		0x8B, 0xCE,								// +50: MOV ECX, ESI
		0x88, 0x46, 0x4D;						// +52: MOV [ESI+4Dh], AL

	patch1 +=
		0xE9, 0x01FF, 0x01FF, 0x01FF, 0x01FF,	// +3B: JMP LONG patchFSAA
		0x90,									//      NOP
		-1, -1, -1, -1, -1, -1, -1,	-1, -1, -1,
		-1, -1, -1, -1, -1,	-1, -1,	-1, -1, -1;

	MemoryPatch mp1( NULL, patch1, backup1 );
	mp1.Search( L"Pleione.dll" );
	mp1.PatchRelativeAddress( 0x01, (LPBYTE)patchFSAA );

	addrTargetReturn = mp1.GetAddr() + 6;	// Size of patch

	vector<WORD> patch2;
	vector<WORD> backup2;
	backup2 +=
		0x83, 0x4D, 0xFC, 0xFF,				// +59E: OR [EBP-04h], 0xFFFFFFFF
		0x8D, 0x4D, 0x08,					// +5A2: LEA ECX, [EBP+08h]
		0xFF, 0xD6,							// +5A5: CALL ESI
		0x8B, 0x8B, 0x1C, 0x01, 0x00, 0x00,	// +5A7: MOV ECX, [EBX+11Ch]
		0x8B, 0x01,							// +5AD: MOV EAX, [ECX]
		0x6A, 0x00,							// +5AF: PUSH 0x00
		0xFF, 0x50, 0x4C,					// +5B1: CALL DWORD PTR [EAX+4Ch]
		0x6A, 0x0B;							// +5B4: PUSH 0x0B
	patch2 +=
		-1, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1,
		0x6A, 0x01,							// +5AF: PUSH 0x00 => PUSH 0x01
		-1, -1, -1, -1, -1;

	MemoryPatch mp2( NULL, patch2, backup2 );
	mp2.Search( L"Pleione.dll" );

	patches += mp1;
	patches += mp2;
	if (CheckPatches())
		WriteLog("Patch initialization successful: %s.\n", patchname.c_str());
	else
		WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
}

//-----------------------------------------------------------------------------
// Hook functions

NAKED void CPatcher_EnableFSAA::patchFSAA(void)
{
	__asm {
		pushad;
		pushf;
		call	addrReadFSAA;
		mov		ecx,esi;
		mov		[esi+0x10],eax;
		call	addrReadFSAAQuality;
		mov		ecx,esi;
		mov		[esi+0x14],eax;
		popf;
		popad;
		jmp		addrTargetReturn;
	}
}

//-----------------------------------------------------------------------------
// INI Functions

bool CPatcher_EnableFSAA::ReadINI( void )
{
	if ( ReadINI_Bool( L"EnableFSAA" ))
		return Install();
	return true;
}

bool CPatcher_EnableFSAA::WriteINI( void )
{
	WriteINI_Bool( L"EnableFSAA", IsInstalled() );
	return true;
}
*/