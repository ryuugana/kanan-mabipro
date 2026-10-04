#pragma once

#include <string>

#include <Windows.h>

#include "Mod.hpp"

namespace kanan {
    // For players whose game crashes or closes on its own: logs what happened to kananLog.txt and,
    // when the game ends abnormally, writes a minidump to KananCrash\ in the game folder. Costs nothing
    // until an exception happens or the game ends.
    //
    // Also warns when an exception is about to end the game because Windows' exception chain
    // validation (SEHOP) rejects the game's own handler chain: the game's dialogs run as micro threads
    // (ESL.dll) whose chains don't end the way SEHOP requires, so closing one (the mailbox, a spirit
    // weapon chat) can close the game on some PCs.
    //
    // Optionally starts a watcher process (rundll32 running this DLL's WatchGame) that records the
    // game's exit code even when Windows ends it outright, where nothing in the game can run.
    //
    // It's installed as Kanan loads, so a change takes effect the next time the game starts.
    class CrashDiagnostics : public Mod {
    public:
        CrashDiagnostics();

        // Called as Kanan starts, right after the log is opened, with the game's folder (config.txt).
        static void installAtStartup(const std::string& gameFolder, HMODULE self);

        // True when this DLL is loaded by the watcher (rundll32), where Kanan itself must not start.
        static bool isWatcherProcess();

        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        bool m_isEnabled;
        bool m_isWatcherEnabled;
    };
}
