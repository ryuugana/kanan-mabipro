#pragma once

#include <Patch.hpp>

#include "PatchMod.hpp"

namespace kanan {
    // Keeps the sky looking like daytime: the sky time is held between 4:00 and 18:00.
    // Ported from AstralWorld's Disable Nighttime (pleione::CAtmosphere::SetSkyTime, Renderer2.dll).
    class DisableNighttime : public PatchMod {
    public:
        DisableNighttime();

        void onPatchUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        Patch m_patch;
        bool m_enabled;

        void apply();
    };
}
