#pragma once

#include <vector>

#include <Patch.hpp>

#include "PatchMod.hpp"

namespace kanan {
    // Shows the combat power number above characters' names (ported from Fantasia's ShowCombatPower).
    class ShowCombatPower : public PatchMod {
    public:
        ShowCombatPower();

        void onPatchUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        bool m_showCombatPower;
        bool m_isReady;
        std::vector<Patch> m_patches;

        void apply();
    };
}
