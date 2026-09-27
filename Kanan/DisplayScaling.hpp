#pragma once

#include <string>

#include "Mod.hpp"

namespace kanan {
    // Makes the game render at the screen's full resolution with Windows display scaling above 100%.
    // The game doesn't tell Windows it handles scaling, so Windows gives it a smaller screen and
    // stretches its window to fit, which blurs everything. The UI then looks smaller, which UI Scale
    // can make up for.
    //
    // It has to be set before the game creates its window, so it's applied when Kanan loads, and a
    // change takes effect the next time the game starts.
    class DisplayScaling : public Mod {
    public:
        DisplayScaling();

        // Called as Kanan loads (from DllMain), with the game's folder, where Kanan's config is.
        static void applyAtStartup(const std::string& folder);

        void onFrame() override;
        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        bool m_isEnabled;
        bool m_isWindowChecked;
    };
}
