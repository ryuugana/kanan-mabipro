#include <imgui.h>

#include <Scan.hpp>

#include "Log.hpp"
#include "DisableFlashyDyes.hpp"

using namespace std;

namespace kanan {
    static uintptr_t g_flashyDyesReturn{ 0 };

    // Runs where the game copies an item's three dye colors (a pointer to them is at [ebp+18h]).
    // A color whose top byte is 40h-7Fh (top two bits 01) is flashy. Flashy colors get their top
    // byte replaced, keeping the RGB. Like AstralWorld, the colors are changed where they are read
    // from: the game passes the same pointer on to more functions after the copy.
    //
    // AstralWorld tested 40000000h <= color <= 7F000000h, which missed top byte 7Fh with any RGB.
    // It put FFh in the top byte; 10h is used here instead, a color closer to the flashy one and the
    // same as Kanan's DisableFlashy uses for inventory items.
    static __declspec(naked) void hookFlashyDyes() {
        __asm {
            mov     eax, dword ptr [ebp + 0x18]     // replaced: mov eax, [ebp+18h]
            push    edx
            XOR     edx, edx
        nextColor:
            mov     ecx, dword ptr [eax + edx * 4]
            test    ecx, 0x80000000
            jnz     skipColor
            test    ecx, 0x40000000
            jz      skipColor
            AND     ecx, 0x00FFFFFF
            OR      ecx, 0x10000000
            mov     dword ptr [eax + edx * 4], ecx
        skipColor:
            inc     edx
            cmp     edx, 3
            jb      nextColor
            pop     edx
            mov     ecx, dword ptr [eax]            // replaced: mov ecx, [eax]
            jmp     g_flashyDyesReturn
        }
    }

    DisableFlashyDyes::DisableFlashyDyes()
        : PatchMod{ "Disable Flashy Dyes", "Shows flashy dyes on worn equipment as their plain color." },
        m_patch{},
        m_enabled{ false }
    {
        // mov eax, [ebp+18h]; mov ecx, [eax]; mov [esi+1Ch], ecx; mov ecx, [eax+4]; mov [esi+20h], ecx
        auto address = scan("Pleione.dll", "8B 45 18 8B 08 89 4E 1C 8B 48 04 89 4E 20");

        if (address) {
            log("[DisableFlashyDyes] Found address %p", *address);

            g_flashyDyesReturn = *address + 5;

            // JMP hookFlashyDyes (replaces the first 5 bytes)
            auto rel = (uintptr_t)&hookFlashyDyes - (*address + 5);

            m_patch.address = *address;
            m_patch.bytes = {
                0xE9,
                (int16_t)(rel & 0xFF), (int16_t)((rel >> 8) & 0xFF),
                (int16_t)((rel >> 16) & 0xFF), (int16_t)((rel >> 24) & 0xFF)
            };
        }
        else {
            log("[DisableFlashyDyes] Failed to find address.");
        }
    }

    void DisableFlashyDyes::onPatchUI() {
        if (m_patch.address == 0) {
            return;
        }

        if (ImGui::Checkbox("Disable Flashy Dyes", &m_enabled)) {
            apply();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows flashy dyes on worn equipment as their plain color.\nChanges show on equipment loaded after turning this on.");
        }
    }

    void DisableFlashyDyes::onConfigLoad(const Config& cfg) {
        m_enabled = cfg.get<bool>("DisableFlashyDyes.Enabled").value_or(false);

        if (m_enabled) {
            apply();
        }
    }

    void DisableFlashyDyes::onConfigSave(Config& cfg) {
        cfg.set<bool>("DisableFlashyDyes.Enabled", m_enabled);
    }

    void DisableFlashyDyes::apply() {
        if (m_patch.address == 0) {
            return;
        }

        log("[DisableFlashyDyes] Toggling %s", m_enabled ? "on" : "off");

        if (m_enabled) {
            patch(m_patch);
        }
        else {
            undoPatch(m_patch);
        }
    }
}
