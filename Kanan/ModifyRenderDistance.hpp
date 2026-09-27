#pragma once

#include <Patch.hpp>

#include "Mod.hpp"

namespace kanan {
    // Sets how far away the game draws the world.
    class ModifyRenderDistance : public Mod {
    public:
        ModifyRenderDistance();

        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        bool m_isEnabled;
        int m_distance;
        bool m_isReady;
        Patch m_patch;

        void apply();
    };
}
