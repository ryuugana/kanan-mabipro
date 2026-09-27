#pragma once

#include <memory>
#include <string>

#include <FunctionHook.hpp>

#include "Mod.hpp"

namespace kanan {
    // Chooses how the game shows amounts of gold: with commas (46,500), short (46.5k), or in
    // thousands and millions (46k500 or 46k 500).
    class GoldFormat : public Mod {
    public:
        enum Style : int {
            GAME,
            COMMAS,
            SHORT,
            LETTERS,
            SPACED_LETTERS,
            STYLE_COUNT,
        };

        GoldFormat();

        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

        static std::wstring format(unsigned long amount, Style style);

    private:
        int m_style;

        std::unique_ptr<FunctionHook> m_hook;

        static void* __cdecl getMoneyString(void* string, unsigned long amount, int language);
    };
}
