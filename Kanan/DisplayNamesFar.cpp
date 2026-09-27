#include "Log.hpp"
#include "DisplayNamesFar.hpp"

using namespace std;

namespace kanan {
    // Name display distance used instead of the game's 3000 (same value as Fantasia).
    static const float g_nameVisionRange{ 30000.0f };

    DisplayNamesFar::DisplayNamesFar()
        : PatchMod{ "Display Names From Far", "Shows character, guild and item names from much farther away, without fading." }
    {
        // FLD dword ptr [&g_nameVisionRange] in place of FLD dword ptr [3000.0]. The patterns keep
        // the game's own address of 3000.0 (Pleione.dll always loads at its preferred base), so
        // they only match while the original value is still referenced.
        auto range = (uintptr_t)&g_nameVisionRange;
        vector<int16_t> fldRange{
            -1, -1,
            (int16_t)(range & 0xFF), (int16_t)((range >> 8) & 0xFF),
            (int16_t)((range >> 16) & 0xFF), (int16_t)((range >> 24) & 0xFF)
        };

        // Vision range 1
        addPatch("Pleione.dll", "D9 05 64 1F DD 63 D8 5B 54", 0, fldRange);
        // Vision range 2
        addPatch("Pleione.dll", "D9 05 64 1F DD 63 D8 9E F8 00 00 00 DF E0 F6 C4 41 0F 85 ? ? ? ? 8B 8E E0 00 00 00 53 6A 01 8D 45 D4", 0, fldRange);
        // Vision range (guild)
        addPatch("Pleione.dll", "D9 05 64 1F DD 63 D8 5D 14 DF E0 F6 C4 41 0F 85", 0, fldRange);

        // Vision range (item): JE -> JMP
        addPatch("Pleione.dll", "74 15 A1 ? ? ? ? 8B 48 1C E8 ? ? ? ? 84", 0, { 0xEB });

        // Name fading: JP -> JMP
        // Props
        addPatch("Pleione.dll", "7A 2C D9 46 40 DC 2D ? ? ? ? DC 35 ? ? ? ? D9 5D FC", 0, { 0xEB });
        // (Fantasia: "I forgot")
        addPatch("Pleione.dll", "7A 4D D9 86 F8 00 00 00", 0, { 0xEB });
        // Player name
        addPatch("Pleione.dll", "7A 73 D9 86 F8 00 00 00", 0, { 0xEB });
        // Guild name
        addPatch("Pleione.dll", "7A 12 D9 45 14 DC 2D ? ? ? ? DC 35 ? ? ? ? D9 5D 18 8B", 0, { 0xEB });
        // Item name
        addPatch("Pleione.dll", "7A 2C D9 46 40 DC 2D ? ? ? ? DC 35 ? ? ? ? D9 5D F8", 0, { 0xEB });
        // Guild name 2
        addPatch("Pleione.dll", "7A 12 D9 45 14 DC 2D ? ? ? ? DC 35 ? ? ? ? D9 5D 18 57", 0, { 0xEB });
    }
}
