#pragma once

#include "PatchMod.hpp"

namespace kanan {
    // Shows character, guild and item names from much farther away, without fading them out.
    // Ported from Fantasia's Display Names Far (Pleione.dll). A plain toggle: the base PatchMod
    // handles the UI, config ("DisplayNamesFromFar.Enabled") and undo. It is C++ only because
    // three of the patches point the game at a distance value that lives in Kanan.
    class DisplayNamesFar : public PatchMod {
    public:
        DisplayNamesFar();
    };
}
