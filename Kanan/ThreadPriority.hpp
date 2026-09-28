#pragma once

#include "Mod.hpp"

namespace kanan {
    // Raises the priority of the game's main thread (the one that runs the game and draws it), so
    // Windows runs it first when other programs want the same processor. Like AstralWorld's
    // SetThreadPriority. Works alongside CPU Scheduling, which picks the processors the game uses.
    class ThreadPriority : public Mod {
    public:
        ThreadPriority();

        std::string getName() override { return "Thread Priority"; }

        void onFrame() override;
        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        enum Choice : int {
            GAME_DEFAULT,
            ABOVE_NORMAL,
            HIGH,
            CHOICE_COUNT,
        };

        int m_choice;
        int m_appliedChoice;

        // The main thread's priority before any change, put back for Game default.
        int m_originalPriority;
        bool m_isOriginalKnown;
    };
}
