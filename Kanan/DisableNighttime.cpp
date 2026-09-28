#include <imgui.h>

#include <Scan.hpp>

#include "Log.hpp"
#include "DisableNighttime.hpp"

using namespace std;

namespace kanan {
    static const char* const g_choiceNames[] = { "Game default", "Always day", "Always night" };

    // Replacements for pleione::CAtmosphere::SetSkyTime(float time), where time is the fraction of
    // a day (0.0 = 0:00, 0.5 = 12:00). Same logic as AstralWorld.

    // Stores the time, but held between 4:00 and 18:00. The float bit patterns are compared as
    // unsigned integers, which orders positive floats correctly.
    static __declspec(naked) void hookSkyTimeDay() {
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

    // Stores just under 2.0, the end of a day as AstralWorld had it: midnight.
    static __declspec(naked) void hookSkyTimeNight() {
        __asm {
            mov     eax, dword ptr [ecx]
            mov     dword ptr [eax + 0x0C], 0x3FFFFFFF
            ret     4
        }
    }

    // A JMP from ADDRESS to DESTINATION.
    static Patch jumpPatch(uintptr_t address, void* destination) {
        auto rel = (uintptr_t)destination - (address + 5);

        Patch p{};
        p.address = address;
        p.bytes = {
            0xE9,
            (int16_t)(rel & 0xFF), (int16_t)((rel >> 8) & 0xFF),
            (int16_t)((rel >> 16) & 0xFF), (int16_t)((rel >> 24) & 0xFF)
        };

        return p;
    }

    DisableNighttime::DisableNighttime()
        : PatchMod{ "Sky Time", "Keeps the sky looking like daytime or nighttime, whatever the time in game." },
        m_dayPatch{},
        m_nightPatch{},
        m_choice{ GAME_DEFAULT }
    {
        // pleione::CAtmosphere::SetSkyTime (the whole function; 12 bytes).
        auto address = scan("Renderer2.dll", "8B 01 D9 44 24 04 D9 58 0C C2 04 00");

        if (address) {
            log("[SkyTime] Found SetSkyTime at %p", *address);

            m_dayPatch = jumpPatch(*address, &hookSkyTimeDay);
            m_nightPatch = jumpPatch(*address, &hookSkyTimeNight);
        }
        else {
            log("[SkyTime] Failed to find SetSkyTime.");
        }
    }

    void DisableNighttime::onPatchUI() {
        if (m_dayPatch.address == 0) {
            return;
        }

        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);

        if (ImGui::Combo("Sky Time", &m_choice, g_choiceNames, CHOICE_COUNT)) {
            apply();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Always day keeps the sky looking like daytime between 18:00 and 4:00.\n"
                "Always night keeps it looking like midnight all day.");
        }
    }

    void DisableNighttime::onConfigLoad(const Config& cfg) {
        // Disable Nighttime's setting, from before it had the nighttime choice, is Always day.
        auto wasDisableNighttime = cfg.get<bool>("DisableNighttime.Enabled").value_or(false);

        m_choice = cfg.get<int>("SkyTime.Choice").value_or(wasDisableNighttime ? ALWAYS_DAY : GAME_DEFAULT);

        if (m_choice < GAME_DEFAULT || m_choice >= CHOICE_COUNT) {
            m_choice = GAME_DEFAULT;
        }

        apply();
    }

    void DisableNighttime::onConfigSave(Config& cfg) {
        cfg.set<int>("SkyTime.Choice", m_choice);
    }

    void DisableNighttime::apply() {
        if (m_dayPatch.address == 0) {
            return;
        }

        log("[SkyTime] %s", g_choiceNames[m_choice]);

        // Both replace the same function: put the game's back, then the chosen one in.
        undoPatch(m_dayPatch);
        undoPatch(m_nightPatch);

        if (m_choice == ALWAYS_DAY) {
            patch(m_dayPatch);
        }
        else if (m_choice == ALWAYS_NIGHT) {
            patch(m_nightPatch);
        }
    }
}
