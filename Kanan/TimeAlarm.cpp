/*#include "Patcher_TimeAlarm.h"
#include "../Addr.h"
#include "../FileSystem.h"

//-----------------------------------------------------------------------------
// Static variable initialization
wchar_t CPatcher_TimeAlarm::dataBuf[256];

int CPatcher_TimeAlarm::CurNo = 0;

int CPatcher_TimeAlarm::tempRinged=0;
int CPatcher_TimeAlarm::tempUsing;
int CPatcher_TimeAlarm::tempHour;
int CPatcher_TimeAlarm::tempHour1;
int CPatcher_TimeAlarm::tempHour2;
int CPatcher_TimeAlarm::tempMin;
int CPatcher_TimeAlarm::tempMin1;
int CPatcher_TimeAlarm::tempMin2;
int CPatcher_TimeAlarm::tempCode;
wchar_t *CPatcher_TimeAlarm::tempText;

int CPatcher_TimeAlarm::AlarmRinged[10];
int CPatcher_TimeAlarm::AlarmUsing[10];
int CPatcher_TimeAlarm::AlarmHour[10];
int CPatcher_TimeAlarm::AlarmHour1[10];
int CPatcher_TimeAlarm::AlarmHour2[10];
int CPatcher_TimeAlarm::AlarmMin[10];
int CPatcher_TimeAlarm::AlarmMin1[10];
int CPatcher_TimeAlarm::AlarmMin2[10];
int CPatcher_TimeAlarm::AlarmCode[10];

int CPatcher_TimeAlarm::DisableDummy;

wchar_t CPatcher_TimeAlarm::AlarmText[10][256];

LPBYTE CPatcher_TimeAlarm::addrAlarm = NULL;
LPBYTE CPatcher_TimeAlarm::addrNoAlarm = NULL;
LPBYTE CPatcher_TimeAlarm::addrTargetReturn = NULL;
LPBYTE CPatcher_TimeAlarm::addrTargetReturn2 = NULL;

LPBYTE CPatcher_TimeAlarm::addrGetGlobalTime = NULL;
LPBYTE CPatcher_TimeAlarm::addrGlobalTimeToGameTime = NULL;
LPBYTE CPatcher_TimeAlarm::addrIsUsingNaosSupport = NULL;
LPBYTE CPatcher_TimeAlarm::addrShowCaption = NULL;
LPBYTE CPatcher_TimeAlarm::addrShowChatMsg = NULL;
LPBYTE CPatcher_TimeAlarm::addrCStringTUni_wchar = NULL;
LPBYTE CPatcher_TimeAlarm::addrGetObjectId = NULL;
LPBYTE CPatcher_TimeAlarm::addrUsableContents = NULL;

wchar_t CPatcher_TimeAlarm::NaoRecharged[] = L"code.client.msg.nao_support.recharged";

bool CPatcher_TimeAlarm::PatchEnabled = false;
//-----------------------------------------------------------------------------
// Constructor

CPatcher_TimeAlarm::CPatcher_TimeAlarm( void )
{
	patchname = "Time Alarm";

	addrGetGlobalTime = CAddr::Addr(stdapi_GetGlobalTime);
	addrGlobalTimeToGameTime = CAddr::Addr(stdapi_GlobalTimeToGameTime);
	addrIsUsingNaosSupport = CAddr::Addr(IServiceMgr_IsUsingNaosSupport);
	addrShowCaption = CAddr::Addr(stdapi_ShowCaption);
	addrShowChatMsg = CAddr::Addr(stdapi_ShowChattingMessage);
	addrUsableContents = CAddr::Addr(IServiceMgr_IsUsableContents);
	if (!addrGetGlobalTime ||
		!addrGlobalTimeToGameTime ||
		!addrIsUsingNaosSupport ||
		!addrShowCaption ||
		!addrShowChatMsg ||
		!addrUsableContents) {
		WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
		WriteLog("   Missing dependency.\n");
		return;
	}

	vector<WORD> patch1;
	vector<WORD> patch2;
	vector<WORD> patch3;
	vector<WORD> backup1;
	vector<WORD> backup2;
	vector<WORD> backup3;

	backup1 +=
		0x6A, 0x14,
		0x90,
		0xE8, -1, -1, -1, -1;			// CALL DWORD PTR DS:[xxxxxxxx] (Standard.?IsUsableContents)

	patch1 +=
		0x90, 0x90,
		0x90,
		0xE8, 0x01FF, 0x01FF, 0x01FF, 0x01FF;

	MemoryPatch mp1( (LPBYTE)0x63929CA0, patch1, backup1 );
	mp1.PatchRelativeAddress(0x01, (LPBYTE)patchTimeAlarm);

	// addrAlarm = mp1.GetAddr() + 0xB1;
	addrAlarm = (LPBYTE)0x63929D38;
	addrNoAlarm = mp1.GetAddr() + 0x84;
	// addrNoAlarm = (LPBYTE)0x63929D2A;

	backup2 +=
		0xC6, 0x45, 0xFC, 0x03,					// +1D0: MOV BYTE PTR SS:[EBP-4],3
		0x90,
		0xE8, -1, -1, -1, -1,					// +1D4: CALL DWORD PTR DS:[xxxxxxxx] (ESL.??BCFormatter@esl@@QBE?AV?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@1@XZ)
		0x33, 0xC0,								// +1DA: XOR EAX, EAX
		0x50,									// +1DC: PUSH EAX
		0x50,									// +1DD: PUSH EAX
		0x6A, 0xFF,								// +1DE: PUSH -1
		0x57,									// +1E0: PUSH EDI
		0x57,									// +1E1: PUSH EDI
		0x6A, 0x04;								// +1E2: PUSH 4

	funcPointer2 = (LPBYTE)patchTimeAlarmCaption;

	patch2 +=
		-1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1,
		-1, -1,
		0x50,
		0x50,
		0xFF,
		0x25,
		((int)(&funcPointer2) & 0xFF),
		(((int)(&funcPointer2) & 0xFF00) >> 8),
		(((int)(&funcPointer2) & 0xFF0000) >> 16),
		(((int)(&funcPointer2) & 0xFF000000) >> 24);

	MemoryPatch mp2( (LPBYTE)0x63929D64, patch2, backup2 );

	addrTargetReturn = mp2.GetAddr() + 0x14;

	patches += mp1;
	patches += mp2;

	if (CheckPatches())
		WriteLog("Patch initialization successful: %s.\n", patchname.c_str());
	else
		WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
}

//-----------------------------------------------------------------------------
// Hook functions

NAKED void CPatcher_TimeAlarm::patchTimeAlarm(void)
{
	__asm {
		PUSH	EAX
	}

	if (IsPatchEnabled() == false)
	{
		if (DisableDummy == 0)
		{
			AlarmRinged[0] = 0;
			AlarmRinged[1] = 0;
			AlarmRinged[2] = 0;
			AlarmRinged[3] = 0;
			AlarmRinged[4] = 0;
			AlarmRinged[5] = 0;
			AlarmRinged[6] = 0;
			AlarmRinged[7] = 0;
			AlarmRinged[8] = 0;
			AlarmRinged[9] = 0;
			DisableDummy = 1;
		}
		__asm {
			POP		EAX		
			CALL	addrUsableContents				// DS:[63CF1878]=019878EC (Standard.?IsUsingNaosSupport@IServiceMgr@core@@QBE_NXZ)
		}
	}
		tempRinged = AlarmRinged[CurNo];
		tempHour = AlarmHour[CurNo];
		tempHour1 = AlarmHour1[CurNo];
		tempHour2 = AlarmHour2[CurNo];
		tempMin = AlarmMin[CurNo];
		tempMin1 = AlarmMin1[CurNo];
		tempMin2 = AlarmMin2[CurNo];
		tempCode = AlarmCode[CurNo];
		tempUsing = AlarmUsing[CurNo];
		tempText = (wchar_t *)&AlarmText[CurNo];

	// Alarm operation
	if (IsPatchEnabled())
	{
		__asm {
			POP		EAX
			MOV		DisableDummy, 0
			ADD		ESP, 4h							// Stack POP
			MOV		EDI, 1388h
			LEA		EAX, DWORD PTR SS:[EBP-28h]		// Game Time; Minute
			PUSH	EAX
			LEA		EAX, DWORD PTR SS:[EBP-1Ch]		// Game Time; Hour
			PUSH	EAX
			LEA		EAX, DWORD PTR SS:[EBP-18h]		// Game Time; (Unknown)
			PUSH	EAX
			CALL	addrGetGlobalTime
			PUSH	EDX
			PUSH	EAX
			CALL	addrGlobalTimeToGameTime
			ADD		ESP, 14h

			// Sort by usage
			CMP		tempUsing, 0
			JE		lEnd2
			
			// Jump when the ring time is fixed
			CMP		tempHour, 24
			MOV		EAX, tempHour1
			JNE		lNext

			// Time does not matter
			MOV		EAX, tempMin1
			CMP		EAX, tempMin2
			JA		lAlarmHour24Right
			JMP		lAlarmHour24Left

			// AlarmMin1 < AlarmMin2
			lAlarmHour24Left:
			MOV		EAX, tempMin1
			CMP		DWORD PTR SS:[EBP-28h], EAX
			JB		lEnd
			MOV		EAX, tempMin2
			CMP		DWORD PTR SS:[EBP-28h], EAX
			JA		lEnd
			JMP		lAlarm

			// AlarmMin1 > AlarmMin2
			lAlarmHour24Right:
			MOV		EAX, tempMin1
			CMP		DWORD PTR SS:[EBP-28h], EAX
			JA		lAlarm
			MOV		EAX, tempMin2
			CMP		DWORD PTR SS:[EBP-28h], EAX
			JB		lAlarm
			JMP		lEnd

			lNext:

			// If the time is fixed
			MOV		EAX, tempHour1
			CMP		EAX, tempHour2
			JA		lMoreHour						// AlarmHour1 > AlarmHour2
			JE		lEqualHour						// AlarmHour1 = AlarmHour2
			JMP		lLessHour						// AlarmHour1 < AlarmHour2


			// 0] 1 2 3 ... 21 22 23 [24
			lMoreHour:
			MOV		EAX, tempHour2
			CMP		DWORD PTR SS:[EBP-1Ch], EAX
			JE		lMoreHour24
			MOV		EAX, tempHour1
			CMP		DWORD PTR SS:[EBP-1Ch], EAX
			JE		lMoreHour23
			JMP		lEnd
			
			lMoreHour23:
			// 23 o'clock
			MOV		EAX, tempMin1
			CMP		DWORD PTR SS:[EBP-28h], EAX
			JAE		lAlarm
			JMP		lEnd									// 23시 57분 이전

			lMoreHour24:
			// 24 o'clock
			MOV		EAX, tempMin2
			CMP		DWORD PTR SS:[EBP-28h], EAX
			JBE		lAlarm
			JMP		lEnd									// 0시 3분 이후

			// same time
			lEqualHour:
			MOV		EAX, tempHour1
			CMP		EAX, DWORD PTR SS:[EBP-1Ch]
			JNE		lEnd									// 시간이 다르다
			JMP		lCompareMin

			// minutes comparison
			lCompareMin:
			MOV		EAX, tempMin1
			CMP		DWORD PTR SS:[EBP-28h], EAX
			JB		lEnd
			MOV		EAX, tempMin2
			CMP		DWORD PTR SS:[EBP-28h], EAX
			JA		lEnd
			JMP		lAlarm

			// 0 1 [2 3] 4 5 ...
			lLessHour:
			MOV		EAX, tempHour1
			CMP		DWORD PTR SS:[EBP-1Ch], EAX
			JE		lLessHourLeft

			MOV		EAX, tempHour2
			CMP		DWORD PTR SS:[EBP-1Ch], EAX
			JE		lLessHourRight
			JMP		lEnd

			lLessHourLeft:
			MOV		EAX, tempMin1
			CMP		DWORD PTR SS:[EBP-28h], EAX
			JB		lEnd
			JMP		lAlarm

			lLessHourRight:
			MOV		EAX, tempMin2
			CMP		DWORD PTR SS:[EBP-28h], EAX
			JA		lEnd
			JMP		lAlarm

			// Alarm processing
			lAlarm:
			CMP		tempRinged, 1					// Jump when alarm is already on
			JE		lEnd2

			MOV		tempRinged, 1
			PUSH	DWORD PTR SS:[EBP-28h]
			PUSH	DWORD PTR SS:[EBP-1Ch]
			PUSH	DWORD PTR DS:[tempText]
			PUSH	offset dataBuf
			CALL	patchFormat
			PUSH	offset dataBuf
		}

		AlarmRinged[CurNo] = tempRinged;

		if (CurNo == 9)
			CurNo = 0;
		else CurNo = CurNo+1;

		__asm {
			JMP		addrAlarm


			// What happened to nothing?
			lEnd:
			MOV		tempRinged, 0
			MOV		EAX, 0
			TEST	EAX, EAX
		}

		AlarmRinged[CurNo] = tempRinged;

		if (CurNo == 9)
			CurNo = 0;
		else CurNo = CurNo+1;

		__asm {
			JMP		addrNoAlarm

			// Alarm delay (already rang once)
			lEnd2:
			MOV		EAX, 0
			TEST	EAX, EAX
		}

		AlarmRinged[CurNo] = tempRinged;

		if (CurNo == 9)
			CurNo = 0;
		else CurNo = CurNo+1;

		__asm {
			JMP		addrNoAlarm
		}
	}/*
	else
	{
		__asm {
			MOV		tempRinged, 0
		}

		AlarmRinged[CurNo] = tempRinged;

		if (CurNo == 9)
			CurNo = 0;
		else CurNo = CurNo+1;

		__asm {
			POP		EAX
			JMP		addrIsUsingNaosSupport				// DS:[63CF1878]=019878EC (Standard.?IsUsingNaosSupport@IServiceMgr@core@@QBE_NXZ)

		}
	}/////////////////////////////////////////////////////////
}

NAKED void CPatcher_TimeAlarm::patchTimeAlarmCaption(void)
{
	__asm {
		PUSH - 1
		PUSH	EDI
		PUSH	EDI

		// 알람 작동
		PUSH	EAX
	}

	if (IsPatchEnabled())
	{
		__asm {
			POP EAX
			PUSH DWORD PTR DS : [tempCode]
		}
	}
	else
	{
		__asm {
			POP EAX
			PUSH offset NaoRecharged
		}
	}

	__asm {
		JMP		addrTargetReturn
	}
}
//-----------------------------------------------------------------------------
// 오버라이드 함수

bool CPatcher_TimeAlarm::Uninstall(void) {
	// This mode can not be removed once installed
	return false;
}

bool CPatcher_TimeAlarm::Toggle(void) {
	// This mode can not be removed once installed
	return false;
}

//-----------------------------------------------------------------------------
// 패치 토글 함수

bool CPatcher_TimeAlarm::IsPatchEnabled(void)
{
	if (PatchEnabled == true) {
		return true;
	}
	else return false;
}

bool CPatcher_TimeAlarm::PatchEnable(void)
{
	PatchEnabled = true;
	return true;
}

bool CPatcher_TimeAlarm::PatchDisable(void)
{
	PatchEnabled = false;
	return true;
}

bool CPatcher_TimeAlarm::PatchToggle(void)
{
	if (IsPatchEnabled())
		PatchDisable();
	else PatchEnable();
	return true;
}

//-----------------------------------------------------------------------------
// Options

bool CPatcher_TimeAlarm::SetString(wchar_t* string)
{
	//wcsncpy_s( AlarmText, 256, string, _TRUNCATE );

	return true;
}
string __fastcall str_replace(const string& str, const string& pattern, const string& replace)
{
	string result = str;
	string::size_type pos = 0;
	string::size_type offset = 0;

	while ((pos = result.find(pattern, offset)) != string::npos)
	{
		result.replace(result.begin() + pos, result.begin() + pos + pattern.size(), replace);
		offset = pos + replace.size();
	}

	return result;
}
void __stdcall CPatcher_TimeAlarm::patchFormat(wchar_t* buff, wchar_t* str, int hour, int min)
{
	swprintf_s(buff, 255, str, (hour + (min + 3) / 60) % 24, (min + 3) % 60);
}
//-----------------------------------------------------------------------------
// INI Functions

bool CPatcher_TimeAlarm::ReadINI(void)
{
	ReadINIAll();
	return true;
}

bool CPatcher_TimeAlarm::ReadINIAll(void)
{
	wchar_t buf[256];
	wchar_t inibuf[256];

	for (int i = 0; i < 10; i++)
	{
		//wcsncpy_s( buf2, 256, string, _TRUNCATE );
		swprintf(inibuf, 256, L"Alarm%d_%s", i + 1, L"Text");
		ReadINI_String(inibuf, buf);
		wcsncpy_s(AlarmText[i], 256, buf, _TRUNCATE);
		//WriteLog("INI READ - %ls: %ls\n", inibuf, AlarmText[i]);
		//wcsncpy_s( AlarmText, 256, string, _TRUNCATE );
		//if (buf != NULL) SetString( buf );

		swprintf(inibuf, 256, L"Alarm%d_%s", i + 1, L"Using");
		AlarmUsing[i] = ReadINI_Int(inibuf, 0, 24);

		swprintf(inibuf, 256, L"Alarm%d_%s", i + 1, L"Hour");
		AlarmHour[i] = ReadINI_Int(inibuf, 0, 24);
		//WriteLog("INI READ - %ls: %d\n", inibuf, AlarmHour[i]);

		swprintf(inibuf, 256, L"Alarm%d_%s", i + 1, L"Min");
		AlarmMin[i] = ReadINI_Int(inibuf, 0, 60);
		//WriteLog("INI READ - %ls: %d\n", inibuf, AlarmMin[i]);

		AlarmMin1[i] = (AlarmMin[i] - 4) % 60;
		AlarmMin2[i] = (AlarmMin[i]) % 60;

		AlarmHour1[i] = (AlarmHour[i] - (AlarmMin[i] / 60)) % 24;
		AlarmHour2[i] = (AlarmHour[i] + (AlarmMin[i] / 60)) % 24;

		if (AlarmMin[i] - 4 < 0)
		{
			AlarmHour1[i] = AlarmHour[i] - 1;
			AlarmMin1[i] = (60 + AlarmMin[i] - 4);
		}

		swprintf(inibuf, 256, L"Alarm%d_%s", i + 1, L"Code");
		AlarmCode[i] = ReadINI_Int(inibuf, 1, 9);
		//WriteLog("INI READ - %ls: %d\n", inibuf, AlarmCode[i]);
		if (AlarmCode[i] < 1 && AlarmCode[i] > 9)
			AlarmCode[i] = 3;
	}

	if (ReadINI_Bool(L"TimeAlarm"))
	{
		PatchEnable();
		return Install();
	}
	return true;
}

bool CPatcher_TimeAlarm::WriteINI(void)
{
	WriteINI_Bool(L"TimeAlarm", IsPatchEnabled());
	return true;
}*/