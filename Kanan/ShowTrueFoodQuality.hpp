#pragma once

#include <Patch.hpp>

#include "PatchMod.hpp"

namespace kanan {
    // Adds the exact quality number after the star rating in food descriptions, for example
    // "<stars> (57)". Ported from AstralWorld's Show True Food Quality.
    //
    // AstralWorld replaced the whole text with operator=; this calls the same operator+= the game
    // itself uses at that spot, so any text the game put before the stars is kept.
    class ShowTrueFoodQuality : public PatchMod {
    public:
        ShowTrueFoodQuality();

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
