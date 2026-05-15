#include <Windows.h>

#include <filesystem>
#include <String.hpp>

#include "Log.hpp"
#include "Kanan.hpp"

using namespace std;
using namespace kanan;

TCHAR g_dllPath[MAX_PATH]{ 0 };
HINSTANCE mHinstDLL = 0;

extern "C" UINT_PTR  mProc = 0;

LPCSTR mImportName = "CreateBandiCapture";

//
// This is the entrypoint for kanan. It's only responsible for setting up the global
// log file and creating the global kanan object.
//
DWORD WINAPI kananInit(LPVOID params) {
    string previousKananDll = "dsound.dll";
    string batRemoveOldKanan = "remove_old_kanan.bat";

    if (filesystem::exists(previousKananDll))
    {
        ofstream batch_file(batRemoveOldKanan);
        batch_file <<
            "echo \"Warning two instances of Kanan detected: " << previousKananDll << " and bdcap32.dll.\n"
            "echo \"Removing old Kanan: " << previousKananDll << "\"\n"
            "timeout /t 5 /nobreak\n"
            "del " << previousKananDll << "\n"
            "MabiProLauncher22.exe\n";
        batch_file.close();

        PROCESS_INFORMATION processInformation = { 0 };
        STARTUPINFOA startupInfo = { 0 };
        BOOL result = CreateProcessA(NULL,
            const_cast<char*>(batRemoveOldKanan.c_str()),
            NULL,
            NULL,
            FALSE,
            CREATE_NO_WINDOW,
            NULL,
            NULL,
            &startupInfo,
            &processInformation);

        if (result) exit(0);
        log("Failed to remove old Kanan: %s", previousKananDll.c_str());
        log("Please remove %s manually from your MabiPro folder.", previousKananDll.c_str());
    }

    // Convert g_dllPath to a path we can use.
    auto path = narrow(g_dllPath);

    path = path.substr(0, path.find_last_of("\\/"));

    // First and most important thing is opening the log file.
    startLog(path + "/kananLog.txt"); 

    log("Welcome to Kanan for Mabinogi.");
    log("Creating Kanan object.");

    g_kanan = make_unique<Kanan>(path);

    log("Leaving kananInit.");

    return 0;
}

struct bdcap32_dll {
	HMODULE dll;
	FARPROC OrignalCreateBandiCapture;
} bdcap32;

__declspec(naked) void FakeCreateBandiCapture() { _asm { jmp[bdcap32.OrignalCreateBandiCapture] } }

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
	char path[MAX_PATH];
	switch (ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH:
		{
			bdcap32.dll = LoadLibrary(L"bdcap23.dll");
			if (bdcap32.dll == false)
			{
				MessageBox(0, L"Kanan cannot load bdcap23.dll library", L"Proxy", MB_ICONERROR);
				ExitProcess(0);
			}
			bdcap32.OrignalCreateBandiCapture = GetProcAddress(bdcap32.dll, "CreateBandiCapture");

			// We don't need DllMain getting invoked for thread attach/detach reasons.
			DisableThreadLibraryCalls(hModule);

			// Get the filepath of this dll.
			GetModuleFileName(hModule, g_dllPath, MAX_PATH);

			// Launch our init thread.
			CreateThread(nullptr, 0, kananInit, nullptr, 0, nullptr);
			break;
		}
		default:
			break;
	}

	return TRUE;
}