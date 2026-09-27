#pragma once

#include <Patch.hpp>

#include "PatchMod.hpp"

namespace kanan {
    // Shows flashy dyes on worn equipment as their plain color.
    // Ported from Fantasia's Disable Flashy Dyes (Pleione.dll). Kanan's own DisableFlashy covers
    // items in the inventory and on the ground; this one covers equipment on characters.
    class DisableFlashyDyes : public PatchMod {
    public:
        DisableFlashyDyes();

        void onPatchUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        Patch m_patch;
        bool m_enabled;

        void apply();
    };
}
