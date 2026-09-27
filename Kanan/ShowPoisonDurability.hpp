#pragma once

#include <vector>

#include <Patch.hpp>

#include "PatchMod.hpp"

namespace kanan {
    // Adds "Poison Durability: N/100" to the description of poisoned items.
    // Ported from Fantasia's Show Poison Durability.
    class ShowPoisonDurability : public PatchMod {
    public:
        ShowPoisonDurability();

        void onPatchUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        bool m_enabled;
        bool m_isAvailable;
        // One hook in core::IItem::IsPoisoned plus the "</color>" text of each item description
        // that shows the poison line.
        std::vector<Patch> m_patches;

        void apply();
    };
}
