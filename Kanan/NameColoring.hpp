#pragma once

#include <Patch.hpp>

#include "PatchMod.hpp"

namespace kanan {
    // Colors the names shown while holding ALT by what the character is: humans, elves, giants,
    // pets, friendly NPCs and monsters each get their own color.
    class NameColoring : public PatchMod {
    public:
        NameColoring();

        void onPatchUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        bool m_isEnabled;
        bool m_isReady;
        Patch m_patch;

        void apply();
    };
}
