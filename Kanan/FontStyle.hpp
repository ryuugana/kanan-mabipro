#pragma once

#include <vector>

#include <Patch.hpp>

#include "PatchMod.hpp"

namespace kanan {
    // Chooses the interface text style: the game's default, TrueType fonts (whatever the client's
    // language), or bitmap fonts (which can stop lag when opening windows). The two don't work
    // together, so only one can be picked.
    class FontStyle : public PatchMod {
    public:
        enum Style : int {
            DEFAULT,
            TRUETYPE,
            BITMAP,
            STYLE_COUNT,
        };

        FontStyle();

        void onPatchUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        std::vector<Patch> m_trueTypePatches;
        std::vector<Patch> m_bitmapPatches;
        int m_style;

        void apply();
    };
}
