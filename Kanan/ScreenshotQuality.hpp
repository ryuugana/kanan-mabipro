#pragma once

#include <vector>

#include <Patch.hpp>

#include "Mod.hpp"

namespace kanan {
    // Sets the JPEG quality of screenshots (ported from Fantasia's ScreenshotQuality).
    class ScreenshotQuality : public Mod {
    public:
        ScreenshotQuality();

        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        bool m_isEnabled;
        int m_quality;
        std::vector<Patch> m_patches;

        void apply();
    };
}
