#pragma once

#include <Patch.hpp>

#include "PatchMod.hpp"

namespace kanan {
    class DisplayNamesFar : public PatchMod {
    public:
        DisplayNamesFar();

        void onPatchUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        std::optional<uintptr_t> address1;
        std::optional<uintptr_t> address2;
        std::optional<uintptr_t> address3;
        std::optional<uintptr_t> address4;
        std::optional<uintptr_t> address5;
        std::optional<uintptr_t> address6;
        std::optional<uintptr_t> address7;
        std::optional<uintptr_t> address8;
        std::optional<uintptr_t> address9;
        std::optional<uintptr_t> address10;
        Patch m_patch[10];
        bool m_choice;

        void apply();
    };
}
