#include <imgui.h>

#include <Scan.hpp>

#include "Log.hpp"
#include "NameColoring.hpp"

using namespace std;

namespace kanan {
    // G13 client (Pleione.dll), where a character's ALT name gets its colors. The name object is in
    // esi (text color at +0x58, outline color at +0x5C, both 0xAARRGGBB), the character in edi:
    //   jmp +0Eh                             (you: keep the colors just set)
    //   mov dword ptr [esi+58h], FF3366FFh   (everyone else)
    //   mov dword ptr [esi+5Ch], 303366FFh
    //   mov ecx, edi
    //   nop
    //   call core::ICharacter::IsNPC
    // The call is pointed at our hook, which sets the colors and then calls IsNPC itself.
    namespace {
        constexpr auto namePattern = "EB 0E C7 46 58 FF 66 33 FF C7 46 5C FF 66 33 30 8B CF 90 E8";
        constexpr uintptr_t callOffset = 0x13;
        constexpr uintptr_t textColorOffset = 0x58;
        constexpr uintptr_t outlineColorOffset = 0x5C;

        struct NameColor {
            uint32_t text;
            uint32_t outline;
        };

        // The colors AstralWorld used.
        constexpr NameColor humanColor{ 0xFF00CCFF, 0x0000CCFF };
        constexpr NameColor elfColor{ 0xFFFFC0CB, 0x00FFC0CB };
        constexpr NameColor giantColor{ 0xFF00CC00, 0x0000CC00 };
        constexpr NameColor petColor{ 0xFFFFCC00, 0x00FFCC00 };
        constexpr NameColor friendlyNPCColor{ 0xFFFF6600, 0x00993300 };
        constexpr NameColor monsterColor{ 0xFFFF00FF, 0x003E62FF };
    }

    using IsCharacterType = bool(__thiscall*)(uintptr_t character);

    static IsCharacterType g_nameIsElf{ nullptr };
    static IsCharacterType g_nameIsGiant{ nullptr };
    static IsCharacterType g_nameIsPet{ nullptr };
    static IsCharacterType g_nameIsGoodNPC{ nullptr };
    static IsCharacterType g_nameIsNPC{ nullptr };

    static void __stdcall setNameColor(uintptr_t character, uintptr_t name) {
        auto color = humanColor;

        if (g_nameIsElf(character)) {
            color = elfColor;
        }
        else if (g_nameIsGiant(character)) {
            color = giantColor;
        }
        else if (g_nameIsPet(character)) {
            color = petColor;
        }
        else if (g_nameIsGoodNPC(character)) {
            color = friendlyNPCColor;
        }
        else if (g_nameIsNPC(character)) {
            color = monsterColor;
        }

        *(uint32_t*)(name + textColorOffset) = color.text;
        *(uint32_t*)(name + outlineColorOffset) = color.outline;
    }

    // Stands in for the client's call to IsNPC: sets the colors, then does the call the client
    // made (IsNPC returns straight to the client).
    static __declspec(naked) void hookNameColor() {
        __asm {
            push    esi
            push    edi
            call    setNameColor
            mov     ecx, edi
            jmp     dword ptr [g_nameIsNPC]
        }
    }

    NameColoring::NameColoring()
        : PatchMod{ "Name Coloring", "" },
        m_isEnabled{ false },
        m_isReady{ false },
        m_patch{}
    {
        log("[NameColoring] Entering constructor...");

        auto standard = GetModuleHandleA("Standard.dll");

        if (standard != nullptr) {
            g_nameIsElf = (IsCharacterType)GetProcAddress(standard, "?IsElf@ICharacter@core@@QBE_NXZ");
            g_nameIsGiant = (IsCharacterType)GetProcAddress(standard, "?IsGiant@ICharacter@core@@QBE_NXZ");
            g_nameIsPet = (IsCharacterType)GetProcAddress(standard, "?IsPet@ICharacter@core@@QBE_NXZ");
            g_nameIsGoodNPC = (IsCharacterType)GetProcAddress(standard, "?IsGoodNPC@ICharacter@core@@QBE_NXZ");
            g_nameIsNPC = (IsCharacterType)GetProcAddress(standard, "?IsNPC@ICharacter@core@@QBE_NXZ");
        }

        if (g_nameIsElf == nullptr || g_nameIsGiant == nullptr || g_nameIsPet == nullptr ||
            g_nameIsGoodNPC == nullptr || g_nameIsNPC == nullptr)
        {
            log("[NameColoring] Failed to find the character type functions");
            log("[NameColoring] Leaving constructor");
            return;
        }

        auto address = scan("Pleione.dll", namePattern);

        if (!address) {
            log("[NameColoring] Failed to find the name color code");
            log("[NameColoring] Leaving constructor");
            return;
        }

        auto call = *address + callOffset;
        auto target = call + 5 + *(int32_t*)(call + 1);

        // Only take over the call if it is the IsNPC call expected.
        if (target != (uintptr_t)g_nameIsNPC) {
            log("[NameColoring] The name color code at %p isn't the one expected (calls %p)", *address, target);
            log("[NameColoring] Leaving constructor");
            return;
        }

        auto rel = (int32_t)((uintptr_t)&hookNameColor - (call + 5));

        m_patch.address = call + 1;
        m_patch.bytes = {
            (int16_t)(rel & 0xFF), (int16_t)((rel >> 8) & 0xFF), (int16_t)((rel >> 16) & 0xFF), (int16_t)((rel >> 24) & 0xFF)
        };
        m_isReady = true;

        log("[NameColoring] Found the name color code at %p", *address);
        log("[NameColoring] Leaving constructor");
    }

    void NameColoring::apply() {
        if (!m_isReady) {
            return;
        }

        log("[NameColoring] Toggling %s", m_isEnabled ? "on" : "off");

        if (m_isEnabled) {
            patch(m_patch);
        }
        else {
            undoPatch(m_patch);
        }
    }

    void NameColoring::onPatchUI() {
        if (!m_isReady) {
            return;
        }

        if (ImGui::Checkbox("Name Coloring", &m_isEnabled)) {
            apply();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Colors the names shown while holding ALT by character type:\n"
                "humans light blue, elves pink, giants green, pets yellow,\n"
                "friendly NPCs orange and monsters magenta.");
        }
    }

    void NameColoring::onConfigLoad(const Config& cfg) {
        m_isEnabled = cfg.get<bool>("NameColoring.Enabled").value_or(false);

        if (m_isEnabled) {
            apply();
        }
    }

    void NameColoring::onConfigSave(Config& cfg) {
        cfg.set<bool>("NameColoring.Enabled", m_isEnabled);
    }
}
