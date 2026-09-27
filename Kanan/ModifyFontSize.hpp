#pragma once

#include <Patch.hpp>

#include "Mod.hpp"

namespace kanan {
    // Sets the size of the game's TrueType font text.
    class ModifyFontSize : public Mod {
    public:
        ModifyFontSize();

        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        bool m_isEnabled;
        int m_size;
        int m_originalSize;
        bool m_isReady;
        Patch m_patch;

        void apply();
    };
}
