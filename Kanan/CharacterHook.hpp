#pragma once

#include <cstdint>
#include <functional>

namespace kanan {
    // Shared hook on pleione::CCharacter's per-frame render update (Pleione.dll), which also
    // positions the character's in-world name. Callbacks run after the original, once per
    // character per frame, on the game thread. Several mods need this function and it can only
    // be hooked once.
    using CharacterUpdateCallback = std::function<void(uintptr_t character)>;

    // Returns false if the update function could not be found or hooked.
    bool addCharacterUpdateCallback(CharacterUpdateCallback callback);
}
