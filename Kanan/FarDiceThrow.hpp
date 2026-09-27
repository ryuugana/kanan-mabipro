#pragma once

#include <Patch.hpp>

#include "PatchMod.hpp"

namespace kanan {
    // Lets the Dice Tossing skill throw dice much farther away.
    class FarDiceThrow : public PatchMod {
    public:
        FarDiceThrow();

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
