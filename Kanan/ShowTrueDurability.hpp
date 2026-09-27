#pragma once

#include <vector>

#include <Patch.hpp>

#include "Mod.hpp"

namespace kanan {
    // Shows item durability with 1000x precision using a text the player chooses, optionally
    // followed by the item's color codes, and shows full-durability items in a different color.
    // Ported from AstralWorld's Show True Durability (ShowTrueDurability, ShowTrueDurability_str and
    // ShowItemColor in mss32.ini).
    //
    // In the text, {0} is the durability the game shows, {1} the same x1000, {2} the maximum
    // durability, {3} the maximum x1000, for example "Durability {1}/{3} ({0}/{2})".
    class ShowTrueDurability : public Mod {
    public:
        ShowTrueDurability();

        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        bool m_enabled;
        bool m_showItemColor;
        char m_format[256];
        bool m_isAvailable;

        // The durability hooks and the durability text.
        std::vector<Patch> m_patches;
        // Makes consumables and other item types that have no durability line show one too, so
        // their color codes are shown. AstralWorld applied these whenever Show True Durability was on;
        // here they are only applied together with Show Item Color, which is what they are for.
        std::vector<Patch> m_colorPatches;

        void buildText();
        void apply();
    };
}
