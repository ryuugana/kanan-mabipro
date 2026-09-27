#pragma once

#include <Patch.hpp>

#include "Mod.hpp"

namespace kanan {
    // Sets the amount the item split window starts at.
    class ItemSplitQuantity : public Mod {
    public:
        ItemSplitQuantity();

        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        bool m_isEnabled;
        int m_quantity;
        bool m_isReady;
        Patch m_patch;

        void apply();
    };
}
