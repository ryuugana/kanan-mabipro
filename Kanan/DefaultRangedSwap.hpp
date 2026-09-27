#pragma once

#include <Patch.hpp>

#include "PatchMod.hpp"

namespace kanan {
    // Replaces the basic Ranged Attack with another ranged skill.
    // Ported from AstralWorld's Default Ranged Swap (core::ISkillMgr::GetBasicRangedAttackSkill,
    // Standard.dll). Kanan's RangedAttackSwap does the same for the official client but its
    // pattern (client.exe) does not exist in this client.
    class DefaultRangedSwap : public PatchMod {
    public:
        DefaultRangedSwap();

        void onPatchUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        Patch m_patch;
        int m_choice;

        void apply();
    };
}
