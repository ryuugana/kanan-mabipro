#include <cstdint>

#include <Scan.hpp>

#include "Log.hpp"
#include "DisableUndergroundMazeFog.hpp"

namespace kanan {
    //
    // Underground maze fog (Pleione.dll).
    //
    // Maps that minimapinfo.xml gives EnableFog="true" (the mazes of undergroundmaze.xml among
    // them) keep a bitset of the cells the player has explored. The map view's fog refresh walks
    // the rect it's redrawing and, for each pixel (ECX = the map view):
    //
    //   8B 8E 8C 00 00 00   MOV  ECX, [ESI+8Ch]      ; the explored bitset
    //   8A 04 08            MOV  AL, [EAX+ECX]       ; this cell's byte of it
    //   84 D0               TEST AL, DL
    //   75 43               JNZ  explored            ; bit set: draw the map image, alpha FFh
    //   ...
    //   MOV  byte ptr [ECX+EAX*4+3], 0               ; bit clear: transparent, so where the
    //   ...                                          ;  player hasn't been stays black
    //  explored:
    //   OR   dword ptr [EAX], 0FF000000h
    //
    // The cells the player has walked are the trail: they are drawn bright, everything else is
    // simply left out of the map. Making the unexplored pixels dim instead of transparent keeps
    // that trail (explored stays at alpha FFh) while the whole map becomes visible.
    //
    static constexpr auto g_fogPattern =
        "8B 45 FC 8B 4E 30 03 C3 C6 44 81 03 00 EB 24";

    // From the start of the pattern to the alpha byte of the unexplored pixel's MOV.
    static constexpr auto g_alphaOffset = 0x0C;

    // How visible the map is where the player hasn't been, out of 255. The game's own dimmer
    // branch just below this one leaves 0BFh, so this sits at the same kind of level: the map
    // reads as unexplored next to the FFh trail, rather than being gone.
    static constexpr int16_t g_unexploredAlpha = 0xC0;

    DisableUndergroundMazeFog::DisableUndergroundMazeFog()
        : PatchMod{ "Disable Underground Maze Fog",
        "Shows the whole map in the underground mazes, keeping the trail of where you have been." }
    {
        auto address = scan("Pleione.dll", g_fogPattern);

        if (!address) {
            log("[DisableUndergroundMazeFog] Failed to find the map fog code.");
            return;
        }

        auto alpha = *address + g_alphaOffset;

        if (*(uint8_t*)alpha != 0x00) {
            log("[DisableUndergroundMazeFog] The code at %p isn't the one expected, not patching.", alpha);
            return;
        }

        log("[DisableUndergroundMazeFog] Found the map fog code at %p", alpha);

        Patch patch{};

        patch.address = alpha;
        patch.bytes = { g_unexploredAlpha }; // alpha 0 -> dim, instead of leaving the pixel out

        m_patches.emplace_back(patch);
    }
}
