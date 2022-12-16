/*#include "Patcher_ShowTrueFoodQuality.h"
#include "../Addr.h"
#include "../FileSystem.h"

//-----------------------------------------------------------------------------
// Static variable initialization

wchar_t CPatcher_ShowTrueFoodQuality::dataBuf[256];
LPBYTE CPatcher_ShowTrueFoodQuality::addrCStringTEquals = NULL;
LPBYTE CPatcher_ShowTrueFoodQuality::addrTargetReturn = NULL;

//-----------------------------------------------------------------------------
// Constructor

CPatcher_ShowTrueFoodQuality::CPatcher_ShowTrueFoodQuality( void )
{
	patchname = "Show True Food Quality";

	addrCStringTEquals = CAddr::Addr(CStringTUni_Equals);
	if (!addrCStringTEquals) {
		WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
		WriteLog("   Missing dependency.\n");
		return;
	}

	vector<WORD> patch;
	vector<WORD> backup;

	backup +=
		0x8B, 0xCB,								// +CC: MOV ECX, EBX
		0x90, 0xE8, -1, -1, -1, -1,				// +CE: CALL xxxxxxxx (esl::CStringT::operator+=(wchar const *))
		0x80, 0x7D, 0x18, 0x00,					// +D4: CMP [EBP-10], 0
		0x0F, 0x84, 0x31, 0x01, 0x00, 0x00;		// +D8: JZ NEAR +0x131
	patch +=
		0xE9, 0x01FF, 0x01FF, 0x01FF, 0x01FF,	// +CC: JMP patchFoodQuality
		0x90, 0x90, 0x90,						// +D1: NOP (x3)
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1;
	MemoryPatch mp( NULL, patch, backup );
	mp.Search( L"Pleione.dll" );
	mp.PatchRelativeAddress( 0x01, (LPBYTE)patchFoodQuality );

	addrTargetReturn = mp.GetAddr() + 0x08;		// +D4
	
	patches += mp;
	if (CheckPatches())
		WriteLog("Patch initialization successful: %s.\n", patchname.c_str());
	else
		WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
}

//-----------------------------------------------------------------------------
// Hook functions

NAKED void CPatcher_ShowTrueFoodQuality::patchFoodQuality(void)
{
	//EAX: Numeric food quality
	//EBX: String pointer
	//ECX: none
	__asm {
		pop		ecx				
		push	eax
		push	ecx
		push	offset dataBuf
		call	patchFoodQualityFormat
		add		esp, 0Ch

		push	offset dataBuf
		mov		ecx, ebx;
		call	addrCStringTEquals

		jmp		addrTargetReturn
	}
}

void CPatcher_ShowTrueFoodQuality::patchFoodQualityFormat(wchar_t * buff,wchar_t* str,int d)
{
	swprintf_s(buff,255,L"%s (%d)",str,d);
}

//-----------------------------------------------------------------------------
// INI Functions

bool CPatcher_ShowTrueFoodQuality::ReadINI( void )
{
	if ( ReadINI_Bool( L"ShowTrueFoodQuality" ))
		return Install();
	return true;
}

bool CPatcher_ShowTrueFoodQuality::WriteINI( void )
{
	WriteINI_Bool( L"ShowTrueFoodQuality", IsInstalled() );
	return true;
}*/