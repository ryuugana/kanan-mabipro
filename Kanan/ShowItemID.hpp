#pragma once

#include <Patch.hpp>

#include "PatchMod.hpp"

namespace kanan {
    // Shows the item's ID in item descriptions (ported from Fantasia's ShowItemID).
    class ShowItemID : public PatchMod {
    public:
        ShowItemID();

        void onPatchUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        bool m_isEnabled;
        Patch m_entryPatch;
        Patch m_textPatch;

        void apply();
    };
}
