#pragma once

#include <Patch.hpp>

#include "PatchMod.hpp"

namespace kanan {
    // Shows the progress towards the next exploration level in the character window
    // (ported from Fantasia's ShowExplorationPercent).
    class ShowExplorationPercent : public PatchMod {
    public:
        ShowExplorationPercent();

        void onPatchUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        bool m_isEnabled;
        Patch m_textPatch;
        Patch m_callPatch;

        void apply();
    };
}
