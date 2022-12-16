/*#include "Patcher_ModifyZoomLimit.h"
#include "../Addr.h"
#include "../FileSystem.h"

//-----------------------------------------------------------------------------
// Static variable initialization

float CPatcher_ModifyZoomLimit::zoomLimit = 1500;

//-----------------------------------------------------------------------------
// Constructor

CPatcher_ModifyZoomLimit::CPatcher_ModifyZoomLimit( void )
{
	patchname = "Modify Zoom Limit";

	LPBYTE addrCQuaterViewCamera = CAddr::Addr(CQuaterViewCamera_CQuaterViewCamera);
	if (!addrCQuaterViewCamera) {
		WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
		WriteLog("   Missing dependency.\n");
		return;
	}

	vector<WORD> backup;
	vector<WORD> patch;
	backup +=
		0xD9, 0x05, -1, -1, -1, -1;	// +C0: FLD DWORD PTR DS:xxxxxxxx
	patch +=
		-1, -1, ((int)(&zoomLimit) & 0xFF),
		(((int)(&zoomLimit) & 0xFF00) >> 8),
		(((int)(&zoomLimit) & 0xFF0000) >> 16),
		(((int)(&zoomLimit) & 0xFF000000) >> 24);
	MemoryPatch mp( addrCQuaterViewCamera + 0x57, patch, backup );
	// mp.PatchRelativeAddress(0x01, (LPBYTE)(&zoomLimit));
	
	patches += mp;
	if (CheckPatches())
		WriteLog("Patch initialization successful: %s.\n", patchname.c_str());
	else
		WriteLog("Patch initialization failed: %s.\n", patchname.c_str());
}

//-----------------------------------------------------------------------------
// Options

bool CPatcher_ModifyZoomLimit::SetLimit( int value )
{
	if ( patches.empty() ) return false;
	zoomLimit = value;
	float valueFloat = value * 1.0f;
	/*vector<WORD> newPatch;
	newPatch +=
		-1, -1,
		*((BYTE *)(&valueFloat)),
		*((BYTE *)(&valueFloat)+1),
		*((BYTE *)(&valueFloat)+2),
		*((BYTE *)(&valueFloat)+3);
	patches[0].SetPatch( newPatch );///////////////////////////////////
return true;
}

int CPatcher_ModifyZoomLimit::GetLimit(void)
{
	return zoomLimit;
}

//-----------------------------------------------------------------------------
// INI Functions

bool CPatcher_ModifyZoomLimit::ReadINI(void)
{
	if (ReadINI_Bool(L"ModifyZoomLimit"))
	{
		int zoomSetting = ReadINI_Int(L"ModifyZoomLimit", 1, 50000);
		if (!SetLimit(zoomSetting))
			return false;
		return Install();
	}
	return true;
}

bool CPatcher_ModifyZoomLimit::WriteINI(void)
{
	if (IsInstalled())
		WriteINI_Int(L"ModifyZoomLimit", GetLimit());
	return true;
}*/