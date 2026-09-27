#pragma once

#include <Patch.hpp>

#include "Mod.hpp"

namespace kanan {
    // Changes how far the camera can zoom out (ported from Fantasia's ModifyZoomLimit).
    class ModifyZoomLimit : public Mod {
    public:
        ModifyZoomLimit();

        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        bool m_isEnabled;
        int m_limit;
        Patch m_patch;

        void apply();
    };
}
