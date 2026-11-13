#pragma once

#include <Patch.hpp>

#include "PatchMod.hpp"

namespace kanan {
    // Shows the whole map in the underground mazes, keeping the trail of where the player has
    // been (the explored cells stay brighter than the rest).
    class DisableUndergroundMazeFog : public PatchMod {
    public:
        DisableUndergroundMazeFog();
    };
}
