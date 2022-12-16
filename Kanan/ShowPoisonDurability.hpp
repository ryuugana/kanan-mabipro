#pragma once

#include <Patch.hpp>

#include "PatchMod.hpp"

namespace kanan {
    class ShowPoisonDurability : public PatchMod {
    public:
        ShowPoisonDurability();

        void onPatchUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        std::optional<uintptr_t> address;
        Patch m_patch;
        bool m_choice;

        void apply();
    };
}
