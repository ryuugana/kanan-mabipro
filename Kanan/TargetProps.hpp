#pragma once

#include <Patch.hpp>

#include "PatchMod.hpp"

namespace kanan {
    // Lets CTRL-targeting pick props (not just enemies) while in combat mode.
    // Ported from AstralWorld's Target Props: the combat-mode target filter "enemy" becomes
    // "enemy|prop".
    class TargetProps : public PatchMod {
    public:
        TargetProps();

        void onPatchUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        bool m_enabled;
        bool m_isAvailable;
        Patch m_patch;

        void apply();
    };
}
