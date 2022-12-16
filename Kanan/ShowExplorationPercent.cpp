/*#include "Patcher_ShowExplorationPercent.h"
#include "../Addr.h"
#include "../FileSystem.h"

//-----------------------------------------------------------------------------
// Static variable initialization

wchar_t CPatcher_ShowExplorationPercent::dataExplStr[] = L"Expl Lv{1} {0}%";
wchar_t CPatcher_ShowExplorationPercent::dataPercentStr[8];
LPBYTE CPatcher_ShowExplorationPercent::addrGetExploLevel = NULL;
LPBYTE CPatcher_ShowExplorationPercent::addrGetExploExpPercent = NULL;
LPBYTE CPatcher_ShowExplorationPercent::addrFormatterInsertPwchar = NULL;
LPBYTE CPatcher_ShowExplorationPercent::addrPatch = (LPBYTE)patchFunc;

//-----------------------------------------------------------------------------
// Constructor

CPatcher_ShowExplorationPercent::CPatcher_ShowExplorationPercent( void )
{
	patchname = "Show Exploration Percent";

	addrGetExploLevel = CAddr::Addr(IParameterBase2_GetExploLevel);
	addrGetExploExpPercent = CAddr::Addr(IParameter_GetExploExpPercent);
	addrFormatterInsertPwchar = CAddr::Addr(CFormatter_Insert_pwchar);

	if (!addrGetExploLevel ||
		!addrGetExploExpPercent ||
		!addrFormatterInsertPwchar)
	{
		WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
		WriteLog("   Missing dependency.\n");
		return;
	}

	vector<WORD> patch;
	vector<WORD> backup;

	backup +=
		0x68, -1, -1, -1, -1,		// +F5B: PUSH OFFSET "code.interface.window.character.explo_level"
		0x8D, 0x4D, 0xEC,			// +F60: LEA ECX, [EBP-10h]
		0x90, 0xE8, -1, -1, -1, -1,	// +F63: CALL esl::CStringT::CStringT(wchar_t const *)
		0x8D, 0x45, 0xEC,			// +F69: LEA ECX, [EBP-10h]
		0x50,						// +F6C: PUSH EAX
		0x8D, 0x45, 0xB8,			// +F6D: LEA ECX, [EBP-48h]
		0x50,						// +F70: PUSH EAX
		0xC6, 0x45, 0xFC, 0x46,		// +F71: MOV BYTE PTR [EBP-04h], 40h
		0x90, 0xE8, -1, -1, -1, -1,	// +F75: CALL core::CLocalizer::Instance(void)
		0x8B, 0xC8,					// +F7B: MOV ECX, EAX
		0x90, 0xE8, -1, -1, -1, -1,	// +F7D: CALL core::CLocalizer::GetLocalText(CStringT const &)
		0x89, 0x45, 0xE8,			// +F83: MOV [EBP-14h], EAX
		0x8B, 0x4D, 0xDC,			// +F86: MOV ECX, [EBP-20h]
		0x8D, 0x45, 0xE4,			// +F89: LEA EAX, [EBP-1Ch]
		0x50,						// +F8C: PUSH EAX
		0xC6, 0x45, 0xFC, 0x47,		// +F8D: MOV BYTE PTR [EBP-04h], 41h
		0x90, 0xE8, -1, -1, -1, -1;	// +F91: CALL core::IParameterBase2::GetExploLevel(void)
	patch +=
		-1, 0x01FF, 0x01FF, 0x01FF, 0x01FF,		// +F5B: PUSH OFFSET dataExplStr
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, 0x02FF, 0x02FF, 0x02FF, 0x02FF;	// +F91: CALL patchFunc
	MemoryPatch mp( NULL, patch, backup );
	mp.Search( L"Pleione.dll" );
	mp.PatchExactAddress( 0x01, (LPBYTE)(&dataExplStr) );
	mp.PatchExactAddress( 0x02, (LPBYTE)(&addrPatch) );
	
	patches += mp;
	if (CheckPatches())
		WriteLog("Patch initialization successful: %s.\n", patchname.c_str());
	else
		WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
}

//-----------------------------------------------------------------------------
// Hook functions

NAKED void CPatcher_ShowExplorationPercent::patchFunc(void)
{
	__asm {
		PUSH	ECX							// IParameterBase2
		CALL	addrGetExploExpPercent
		SUB		ESP, 4h						// Push the returned value onto the stack
		FSTP	DWORD PTR SS:[ESP]			// ||
		PUSH	offset dataPercentStr		// Push buffer for percentage string
		CALL	patchFormat					// Format percentage string

		MOV		ECX, SS:[EBP-14h]			// ECX = CFormatter
		PUSH	offset dataPercentStr		// Push formatted percentage string
		CALL	addrFormatterInsertPwchar	// EAX = CFormatter
		MOV		SS:[EBP-14h], EAX			// Store CFormatter back in variable (necessary? dunno)

		POP		ECX							// ECX = IParameterBase2
		JMP		addrGetExploLevel			// Jump to the original called function
	}
}

void __stdcall CPatcher_ShowExplorationPercent::patchFormat(wchar_t * buff, float percent)
{
	swprintf_s(buff,7,L"%.1f",percent*100.0);
}

//-----------------------------------------------------------------------------
// INI Functions

bool CPatcher_ShowExplorationPercent::ReadINI( void )
{
	if ( ReadINI_Bool( L"ShowExplorationPercent" ))
		return Install();
	return true;
}

bool CPatcher_ShowExplorationPercent::WriteINI( void )
{
	WriteINI_Bool( L"ShowExplorationPercent", IsInstalled() );
	return true;
}*/