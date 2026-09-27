#pragma once

#include <set>
#include <string>
#include <vector>

#include "Mod.hpp"

namespace kanan {
    // Finds overlays (MSI Afterburner's RTSS, Steam, Discord, OBS, ...) that hook the game's drawing
    // like Kanan does, and logs them: some stop Kanan's menu from showing. Warns with a popup about
    // the ones known to, since a hidden menu can't show a warning.
    class OverlayDetection : public Mod {
    public:
        OverlayDetection();

        void onFrame() override;

        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        bool m_warnConflicts;
        bool m_isWarned;
        bool m_isChecked;
        unsigned long long m_nextCheck;

        // What has been found, logged once each.
        std::set<std::string> m_findings;
        // The overlays found, for the UI.
        std::vector<std::string> m_overlays;

        void check();
        void addFinding(const std::string& finding, const std::string& overlay, bool hidesMenu);
        void warn(const std::string& overlay);
    };
}
