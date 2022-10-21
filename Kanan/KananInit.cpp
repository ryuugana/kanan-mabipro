#include <Windows.h>

#include <String.hpp>

#include "Log.hpp"
#include "ChatLog.hpp"
#include "Kanan.hpp"

using namespace std;
using namespace kanan;

TCHAR g_dllPath[MAX_PATH]{ 0 };
HINSTANCE mHinstDLL = 0;

extern "C" UINT_PTR  mProcs[12] = { 0 };

LPCSTR mImportNames[] = { "DirectSoundCaptureCreate",
						  "DirectSoundCaptureCreate8",
						  "DirectSoundCaptureEnumerateA",
						  "DirectSoundCaptureEnumerateW",
						  "DirectSoundCreate",
						  "DirectSoundCreate8",
						  "DirectSoundEnumerateA",
						  "DirectSoundEnumerateW",
						  "DirectSoundFullDuplexCreate",
						  "DllCanUnloadNow",
						  "DllGetClassObject",
						  "GetDeviceID" };

typedef int(CALLBACK *DirectSoundEnumerate)(LPVOID, LPVOID);
typedef HRESULT(WINAPI *DirectSoundCreate)(LPCGUID, LPVOID, LPVOID);
typedef HRESULT(WINAPI *DirectSoundCreate8)(LPCGUID, LPVOID, LPVOID);

//
// This is the entrypoint for kanan. It's only responsible for setting up the global
// log file and creating the global kanan object.
//
DWORD WINAPI kananInit(LPVOID params) {
    // Convert g_dllPath to a path we can use.
    auto path = narrow(g_dllPath);

    path = path.substr(0, path.find_last_of("\\/"));

    // First and most important thing is opening the log file.
    startLog(path + "/kananLog.txt");
	startChatLog(path + "/kananChatLog.txt");

    log("Welcome to Kanan for Mabinogi.");
    log("Creating Kanan object.");

    g_kanan = make_unique<Kanan>(path);

    log("Leaving kananInit.");

    return 0;
}

BOOL APIENTRY DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
		TCHAR expandedPath[MAX_PATH];
		ExpandEnvironmentStrings(L"%WINDIR%\\System32\\dsound.dll", expandedPath, MAX_PATH);
		mHinstDLL = LoadLibrary(expandedPath);
		if (!mHinstDLL)
			return (FALSE);
		for (int i = 0; i < 12; i++)
			mProcs[i] = (UINT_PTR)GetProcAddress(mHinstDLL, mImportNames[i]);

        // We don't need DllMain getting invoked for thread attach/detach reasons.
        DisableThreadLibraryCalls(module);

        // Get the filepath of this dll.
        GetModuleFileName(module, g_dllPath, MAX_PATH);

        // Launch our init thread.
        CreateThread(nullptr, 0, kananInit, nullptr, 0, nullptr);
    }

    return TRUE;
}


HRESULT WINAPI DirectSoundCreate_wrapper(LPCGUID lpGuid, LPVOID *ppDS, LPUNKNOWN pUnkOuter)
{
	DirectSoundCreate dsound = (DirectSoundCreate)mProcs[4];
	return dsound(lpGuid, ppDS, pUnkOuter);
}
HRESULT WINAPI DirectSoundCreate8_wrapper(LPCGUID lpGuid, LPVOID *ppDS8, LPUNKNOWN pUnkOuter)
{
	DirectSoundCreate8 dsound = (DirectSoundCreate8)mProcs[5];
	HRESULT result = dsound(lpGuid, ppDS8, pUnkOuter);
	return result;
}