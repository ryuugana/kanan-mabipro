#pragma once

#include <cstdint>

#include "PatchMod.hpp"

namespace kanan {

// Selective Naked Mode: keeps Face (pocket 3) and Hair (pocket 4) clothed while
// rendering all other pockets naked. Hooks the per-pocket render function
// (Pleione.dll+0xE9416) which receives the pocket index as a stack argument,
// unlike Pleione.dll+0xE57C4 which renders the whole character at once with no
// pocket index available.
class SelectiveNakedMode : public PatchMod {
public:
    SelectiveNakedMode();

    void onPatchUI() override;

    void onConfigLoad(const Config& cfg) override;
    void onConfigSave(Config& cfg) override;

private:
    Patch m_hookPatch{};           // Body render hook (FUN_638e9416)
    Patch m_clothingHookPatch{};   // Clothing render hook (FUN_638e9b58)
    bool m_hooked{ false };

    void apply();
    void installHook();
    void removeHook();
};

}