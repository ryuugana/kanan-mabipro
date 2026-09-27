#pragma once

#include <Patch.hpp>

#include "PatchMod.hpp"

namespace kanan {
    // Shows your real maximum HP instead of the value the interface caps it at.
    // Ported from AstralWorld's Show True HP.
    //
    // core::IParameter::GetInterfaceLifeDamaged returns the smaller of GetLifeDamaged and
    // GetLifeMax. AstralWorld made its GetLifeMax call return the value GetLifeDamaged had just
    // loaded (two patches and a shared variable); this redirects that one call to a function that
    // reads the same field directly, which gives the same result with a single patch.
    class ShowTrueHP : public PatchMod {
    public:
        ShowTrueHP();

        void onPatchUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        bool m_enabled;
        bool m_isAvailable;
        Patch m_patch;

        void apply();
    };
}
