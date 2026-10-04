#include <Windows.h>

#include <String.hpp>

#include "Log.hpp"
#include "Kanan.hpp"
#include "DisplayScaling.hpp"
#include "CrashDiagnostics.hpp"

using namespace std;
using namespace kanan;

HINSTANCE mHinstDLL = 0;

// Kanan's files (config, log, patches) are in the game's folder.
static string gameFolder() {
    wchar_t gamePath[MAX_PATH]{};

    GetModuleFileNameW(nullptr, gamePath, MAX_PATH);

    auto path = narrow(gamePath);

    return path.substr(0, path.find_last_of("\\/"));
}

//
// This is the entrypoint for kanan. It's only responsible for setting up the global
// log file and creating the global kanan object.
//
DWORD WINAPI kananInit(LPVOID params) {
    auto path = gameFolder();

    // First and most important thing is opening the log file.
    startLog(path + "/kananLog.txt");

    log("Welcome to Kanan for Mabinogi.");

    // Before anything else can crash.
    CrashDiagnostics::installAtStartup(path, mHinstDLL);

    log("Creating Kanan object.");

    g_kanan = make_unique<Kanan>(path, mHinstDLL);

    log("Leaving kananInit.");

    return 0;
}

// Kanan is a Miles plugin: Kanan.asi in the game's system\mss folder, which the game's own sound
// library (Mss32.dll) loads when the game starts its sound. That leaves every game file as it is.
// Miles calls this in each plugin it loads; Kanan provides no sound services.
extern "C" __declspec(dllexport) int __stdcall RIB_Main(void* provider, unsigned long upDown) {
    return 1;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        // Loaded by Crash Diagnostics' watcher (rundll32), only for WatchGame: Kanan doesn't start.
        if (CrashDiagnostics::isWatcherProcess()) {
            DisableThreadLibraryCalls(hModule);
            return TRUE;
        }

        // Stay loaded even if Miles frees the plugins it has no use for.
        HMODULE self{};

        GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)&RIB_Main, &self);

        // We don't need DllMain getting invoked for thread attach/detach reasons.
        DisableThreadLibraryCalls(hModule);

        // Grab for Kanan
        mHinstDLL = hModule;

        // Before the game creates its window, which keeps the display scaling it's created with.
        // Miles loads its plugins before that.
        DisplayScaling::applyAtStartup(gameFolder());

        // Launch our init thread.
        CreateThread(nullptr, 0, kananInit, nullptr, 0, nullptr);
    }

    return TRUE;
}
