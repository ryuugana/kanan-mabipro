#include <imgui.h>

#include "Log.hpp"
#include "ShowTrueHP.hpp"

namespace kanan {
    // Offset of the float that core::IParameterBase2::GetLifeDamaged returns (0x140 in this build).
    static uintptr_t g_lifeDamagedOffset{ 0 };

    // Stands in for IParameterBase2::GetLifeMax (a __thiscall returning a float in ST0).
    static __declspec(naked) void getTrueLifeMax() {
        __asm {
            mov     eax, ecx
            add     eax, g_lifeDamagedOffset
            fld     dword ptr [eax]
            ret
        }
    }

    static uintptr_t callTarget(uintptr_t call) {
        return call + 5 + *(int32_t*)(call + 1);
    }

    ShowTrueHP::ShowTrueHP()
        : PatchMod{ "Show True HP", "Shows your real maximum HP instead of the capped value." },
        m_enabled{ false },
        m_isAvailable{ false },
        m_patch{}
    {
        log("[ShowTrueHP] Entering constructor...");

        auto standard = GetModuleHandleA("Standard.dll");
        auto getInterfaceLifeDamaged = standard ? (uintptr_t)GetProcAddress(standard, "?GetInterfaceLifeDamaged@IParameter@core@@UAEMXZ") : 0;
        auto getLifeDamaged = standard ? (uintptr_t)GetProcAddress(standard, "?GetLifeDamaged@IParameterBase2@core@@QAEMXZ") : 0;
        auto getLifeMax = standard ? (uintptr_t)GetProcAddress(standard, "?GetLifeMax@IParameterBase2@core@@QAEMXZ") : 0;

        if (getInterfaceLifeDamaged == 0 || getLifeDamaged == 0 || getLifeMax == 0) {
            log("[ShowTrueHP] Failed to find the HP functions.");
            log("[ShowTrueHP] Leaving constructor.");
            return;
        }

        // GetLifeDamaged ends with: pop edi / fld dword ptr [esi+offset] / pop esi / leave / ret
        for (uintptr_t i = 0; i < 0x100; ++i) {
            auto p = (uint8_t*)(getLifeDamaged + i);

            if (p[0] == 0x5F && p[1] == 0xD9 && p[2] == 0x86 && p[7] == 0x5E && p[8] == 0xC9 && p[9] == 0xC3) {
                g_lifeDamagedOffset = *(uint32_t*)(p + 3);
                break;
            }
        }

        // GetInterfaceLifeDamaged: call GetLifeDamaged ... mov ecx, esi / call GetLifeMax
        uintptr_t site{ 0 };
        bool callsLifeDamaged{ false };

        for (uintptr_t i = 0; i < 0x30; ++i) {
            auto p = getInterfaceLifeDamaged + i;

            if (*(uint8_t*)p != 0xE8) {
                continue;
            }

            if (callTarget(p) == getLifeDamaged) {
                callsLifeDamaged = true;
            }
            else if (callsLifeDamaged && callTarget(p) == getLifeMax && *(uint16_t*)(p - 2) == 0xCE8B) {
                site = p;
                break;
            }
        }

        if (g_lifeDamagedOffset == 0 || site == 0) {
            log("[ShowTrueHP] Unexpected code in the HP functions, not patching.");
            log("[ShowTrueHP] Leaving constructor.");
            return;
        }

        auto rel = (uintptr_t)&getTrueLifeMax - (site + 5);

        m_patch.address = site;
        m_patch.bytes = { 0xE8, (int16_t)(rel & 0xFF), (int16_t)((rel >> 8) & 0xFF), (int16_t)((rel >> 16) & 0xFF), (int16_t)((rel >> 24) & 0xFF) };
        m_isAvailable = true;

        log("[ShowTrueHP] Found the HP call at %p (field offset %X)", site, g_lifeDamagedOffset);
        log("[ShowTrueHP] Leaving constructor.");
    }

    void ShowTrueHP::apply() {
        if (!m_isAvailable) {
            return;
        }

        log("[ShowTrueHP] Toggling %s", m_enabled ? "on" : "off");

        if (m_enabled) {
            patch(m_patch);
        }
        else {
            undoPatch(m_patch);
        }
    }

    void ShowTrueHP::onPatchUI() {
        if (!m_isAvailable) {
            return;
        }

        if (ImGui::Checkbox("Show True HP", &m_enabled)) {
            apply();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows your real maximum HP instead of the capped value.");
        }
    }

    void ShowTrueHP::onConfigLoad(const Config& cfg) {
        m_enabled = cfg.get<bool>("ShowTrueHP.Enabled").value_or(false);

        if (m_enabled) {
            apply();
        }
    }

    void ShowTrueHP::onConfigSave(Config& cfg) {
        cfg.set<bool>("ShowTrueHP.Enabled", m_enabled);
    }
}
