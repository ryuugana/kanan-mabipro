/*#include "Patcher_ScreenshotQuality.h"
#include "../Addr.h"
#include "../FileSystem.h"
#include "../MSSHook.h"

//-----------------------------------------------------------------------------
// Constructor

CPatcher_ScreenshotQuality::CPatcher_ScreenshotQuality( void )
{
	patchname = "Screenshot Quality";
}

//-----------------------------------------------------------------------------
// Overriden Patching Functions

bool CPatcher_ScreenshotQuality::Install( void ) {
	return true;
}

bool CPatcher_ScreenshotQuality::Uninstall( void ) {
	return true;
}

bool CPatcher_ScreenshotQuality::Toggle( void ) {
	return true;
}

//-----------------------------------------------------------------------------
// INI Functions

bool CPatcher_ScreenshotQuality::ReadINI( void )
{
	if ( ReadINI_Bool( L"ScreenshotQuality" )) {
		int qualitySetting = ReadINI_Int( L"ScreenshotQuality", 1, 100 );
		SngMSSHook::Instance()->SetSSQuality(qualitySetting);
	}
	return true;
}

bool CPatcher_ScreenshotQuality::WriteINI( void )
{
	WriteINI_Int( L"ScreenshotQuality", SngMSSHook::Instance()->GetSSQuality() );
	return true;
}
*/