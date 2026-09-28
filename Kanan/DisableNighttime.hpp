#pragma once

#include <Patch.hpp>

#include "PatchMod.hpp"

namespace kanan {
    // Sky Time: keeps the sky looking like daytime (the sky time held between 4:00 and 18:00) or
    // nighttime (held at midnight). Ported from AstralWorld's SetSkyTime
    // (pleione::CAtmosphere::SetSkyTime, Renderer2.dll); it was Disable Nighttime before it had the
    // nighttime choice.
    class DisableNighttime : public PatchMod {
    public:
        DisableNighttime();

        void onPatchUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        enum Choice : int {
            GAME_DEFAULT,
            ALWAYS_DAY,
            ALWAYS_NIGHT,
            CHOICE_COUNT,
        };

        Patch m_dayPatch;
        Patch m_nightPatch;
        int m_choice;

        void apply();
    };
}
