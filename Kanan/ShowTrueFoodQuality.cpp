#include <cstdio>

#include <imgui.h>

#include <Scan.hpp>

#include "Log.hpp"
#include "ShowTrueFoodQuality.hpp"

namespace kanan {
    // esl::CStringT::operator+=(const wchar_t*), which the game calls to add the star rating.
    static uintptr_t g_foodAppendText{ 0 };
    static uintptr_t g_foodReturn{ 0 };
    static wchar_t g_foodQualityText[256]{};

    static void __cdecl formatFoodQuality(const wchar_t* stars, int quality) {
        swprintf_s(g_foodQualityText, L"%s (%d)", stars, quality);
    }

    // Replaces the game's "text += stars" with "text += stars (quality)".
    // On entry the star text is on top of the stack, EAX holds the quality and EBX the text.
    static __declspec(naked) void hookFoodQuality() {
        __asm {
            pop     ecx
            push    eax
            push    ecx
            call    formatFoodQuality
            add     esp, 8
            push    offset g_foodQualityText
            mov     ecx, ebx
            call    g_foodAppendText
            jmp     g_foodReturn
        }
    }

    ShowTrueFoodQuality::ShowTrueFoodQuality()
        : PatchMod{ "Show True Food Quality", "Shows the exact quality number next to the stars on food." },
        m_enabled{ false },
        m_isAvailable{ false },
        m_patch{}
    {
        log("[ShowTrueFoodQuality] Entering constructor...");

        // mov ecx, ebx / nop / call CStringT::operator+= / cmp byte ptr [ebp+18h], 0 / je ... / cmp dword ptr [ebp+0Ch], 50h
        auto address = scan("Pleione.dll", "8B CB 90 E8 ? ? ? ? 80 7D 18 00 0F 84 ? ? ? ? 83 7D 0C 50");
        auto esl = GetModuleHandleA("ESL.dll");
        auto appendText = esl ? (uintptr_t)GetProcAddress(esl, "??Y?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QAEAAV01@PB_W@Z") : 0;

        if (!address) {
            log("[ShowTrueFoodQuality] Failed to find the food quality text.");
        }
        else {
            auto site = *address;
            auto callTarget = site + 8 + *(int32_t*)(site + 4);

            if (appendText == 0 || callTarget != appendText) {
                log("[ShowTrueFoodQuality] Unexpected code at %p, not patching.", site);
            }
            else {
                g_foodAppendText = callTarget;
                g_foodReturn = site + 8;

                auto rel = (uintptr_t)&hookFoodQuality - (site + 5);

                m_patch.address = site;
                m_patch.bytes = { 0xE9, (int16_t)(rel & 0xFF), (int16_t)((rel >> 8) & 0xFF), (int16_t)((rel >> 16) & 0xFF), (int16_t)((rel >> 24) & 0xFF), 0x90, 0x90, 0x90 };
                m_isAvailable = true;

                log("[ShowTrueFoodQuality] Found the food quality text at %p", site);
            }
        }

        log("[ShowTrueFoodQuality] Leaving constructor.");
    }

    void ShowTrueFoodQuality::apply() {
        if (!m_isAvailable) {
            return;
        }

        log("[ShowTrueFoodQuality] Toggling %s", m_enabled ? "on" : "off");

        if (m_enabled) {
            patch(m_patch);
        }
        else {
            undoPatch(m_patch);
        }
    }

    void ShowTrueFoodQuality::onPatchUI() {
        if (!m_isAvailable) {
            return;
        }

        if (ImGui::Checkbox("Show True Food Quality", &m_enabled)) {
            apply();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows the exact quality number next to the stars on food.");
        }
    }

    void ShowTrueFoodQuality::onConfigLoad(const Config& cfg) {
        m_enabled = cfg.get<bool>("ShowTrueFoodQuality.Enabled").value_or(false);

        if (m_enabled) {
            apply();
        }
    }

    void ShowTrueFoodQuality::onConfigSave(Config& cfg) {
        cfg.set<bool>("ShowTrueFoodQuality.Enabled", m_enabled);
    }
}
