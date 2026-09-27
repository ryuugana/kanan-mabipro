#include <imgui.h>

#include <Scan.hpp>

#include "Log.hpp"
#include "DisableNighttime.hpp"

using namespace std;

namespace kanan {
    // Replacement for pleione::CAtmosphere::SetSkyTime(float time), where time is the fraction of
    // a day (0.0 = 0:00, 0.5 = 12:00). Same logic as Fantasia: store the time, but held between
    // 4:00 and 18:00. The float bit patterns are compared as unsigned integers, which orders
    // positive floats correctly.
    static __declspec(naked) void hookSetSkyTime() {
        __asm {
            mov     eax, dword ptr [ecx]
            mov     ecx, 0x3E2AAAAD             // 1/6 = 4:00
            cmp     ecx, dword ptr [esp + 4]
            ja      setTime
            mov     ecx, 0x3F400000             // 3/4 = 18:00
            cmp     ecx, dword ptr [esp + 4]
            jb      setTime
            mov     ecx, dword ptr [esp + 4]
        setTime:                                // ecx = time to set
            mov     dword ptr [eax + 0x0C], ecx
            ret     4
        }
    }

    DisableNighttime::DisableNighttime()
        : PatchMod{ "Disable Nighttime", "Keeps the sky looking like daytime between 18:00 and 4:00." },
        m_patch{},
        m_enabled{ false }
    {
        // pleione::CAtmosphere::SetSkyTime (the whole function; 12 bytes).
        auto address = scan("Renderer2.dll", "8B 01 D9 44 24 04 D9 58 0C C2 04 00");

        if (address) {
            log("[DisableNighttime] Found SetSkyTime at %p", *address);

            // JMP hookSetSkyTime
            auto rel = (uintptr_t)&hookSetSkyTime - (*address + 5);

            m_patch.address = *address;
            m_patch.bytes = {
                0xE9,
                (int16_t)(rel & 0xFF), (int16_t)((rel >> 8) & 0xFF),
                (int16_t)((rel >> 16) & 0xFF), (int16_t)((rel >> 24) & 0xFF)
            };
        }
        else {
            log("[DisableNighttime] Failed to find SetSkyTime.");
        }
    }

    void DisableNighttime::onPatchUI() {
        if (m_patch.address == 0) {
            return;
        }

        if (ImGui::Checkbox("Disable Nighttime", &m_enabled)) {
            apply();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Keeps the sky looking like daytime between 18:00 and 4:00.");
        }
    }

    void DisableNighttime::onConfigLoad(const Config& cfg) {
        m_enabled = cfg.get<bool>("DisableNighttime.Enabled").value_or(false);

        if (m_enabled) {
            apply();
        }
    }

    void DisableNighttime::onConfigSave(Config& cfg) {
        cfg.set<bool>("DisableNighttime.Enabled", m_enabled);
    }

    void DisableNighttime::apply() {
        if (m_patch.address == 0) {
            return;
        }

        log("[DisableNighttime] Toggling %s", m_enabled ? "on" : "off");

        if (m_enabled) {
            patch(m_patch);
        }
        else {
            undoPatch(m_patch);
        }
    }
}
