#pragma once

#include <vector>

#include <Patch.hpp>

#include "Mod.hpp"

namespace kanan {
    // Attacking with no skill loaded loads a chosen skill (for example Smash) instead of doing a
    // normal attack. Once that skill is loaded, attacks go through normally so it gets used.
    // Ported from Fantasia's Combat Mastery Swap (Pleione.dll + Standard.dll).
    class CombatMasterySwap : public Mod {
    public:
        CombatMasterySwap();

        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        std::vector<Patch> m_patches;
        bool m_isFound;
        bool m_isEnabled;
        int m_skillID;

        void apply();
    };
}
