#pragma once

#include <Patch.hpp>

#include "PatchMod.hpp"

namespace kanan {
    // Shows the in-game clock in large text.
    class LargeClockText : public PatchMod {
    public:
        LargeClockText();

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
