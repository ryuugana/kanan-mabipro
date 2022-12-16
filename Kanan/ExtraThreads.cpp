/*#include "Patcher_ExtraThreads.h"
#include "../Addr.h"
#include "../FileSystem.h"
#include "../MSSHook.h"
#include "../ModifyWindow.h"
#include "../ThreadWatch.h"

//-----------------------------------------------------------------------------
// Static Variables
int CPatcher_ExtraThreads::thread_priority = THREAD_PRIORITY_NORMAL;
int CPatcher_ExtraThreads::sleep_time = 20000;
//-----------------------------------------------------------------------------
// Constructor

CPatcher_ExtraThreads::CPatcher_ExtraThreads( void )
{
	patchname = "Extra Threads";
}

//-----------------------------------------------------------------------------
// Overriden Patching Functions

bool CPatcher_ExtraThreads::Install( void ) {
	bool result = true;
	result &= CThreadWatch::SpawnWatcher();
	result &= CModifyWindow::SpawnModifier(thread_priority, sleep_time);
	installed = result;
	return result;
}

bool CPatcher_ExtraThreads::Uninstall( void ) {
	// This mod cannot be uninstalled once installed
	return false;
}

bool CPatcher_ExtraThreads::Toggle( void ) {
	// This mod cannot be uninstalled once installed
	return false;
}

//-----------------------------------------------------------------------------
// INI Functions

bool CPatcher_ExtraThreads::ReadINI( void )
{
	if ( installed )
		return false;

	if ( !ReadINI_Bool( L"DisableExtraThreads" )) {
		int cpuReduction = ReadINI_Int( L"CPUReduction", 0, 90 );
		CThreadWatch::SetReductionPercentage( cpuReduction );
		bool cpuOnlyMinimized = ReadINI_Bool( L"CPUReduction_OnlyMinimized" );
		CThreadWatch::SetOptionOnlyMinimized( cpuOnlyMinimized );
		thread_priority = ReadINI_Int(L"SetThreadPriority", -15, 15);
		sleep_time = ReadINI_Int(L"WaitMenuHook", 1000, 120000);
		return Install();
	}

	return true;
}

bool CPatcher_ExtraThreads::WriteINI( void )
{
	if ( installed ) {
		int cpuReduction = ReadINI_Int( L"CPUReduction", 0, 90 );
		CThreadWatch::SetReductionPercentage( cpuReduction );
		bool cpuOnlyMinimized = ReadINI_Bool( L"CPUReduction_OnlyMinimized" );
		CThreadWatch::SetOptionOnlyMinimized( cpuOnlyMinimized );
		WriteINI_Int(L"SetThreadPriority", thread_priority);
		WriteINI_Int(L"WaitMenuHook", sleep_time);
	}
	return true;
}
*/