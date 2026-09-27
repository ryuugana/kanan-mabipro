#include <cstring>

#include <imgui.h>

#include "Log.hpp"
#include "FarDiceThrow.hpp"

using namespace std;

namespace kanan {
    // G13 client (Skill.dll), core::CSkillDiceThrowing::GetSkillUsable reads the throw range from
    // the skill's data into a local at +0x5B:
    //   fld dword ptr [eax+44h]
    //   push 1
    //   fstp dword ptr [esp+18h]
    // Those 9 bytes jump to our hook, which does the same and then puts our range in the local.
    namespace {
        constexpr auto getSkillUsableName =
            "?GetSkillUsable@CSkillDiceThrowing@core@@UBE?AW4ESkillProcessType@2@AAUSSkillContext@2@_KPAW4ECursorType@@PAM@Z";
        constexpr uintptr_t rangeOffset = 0x5B;
        constexpr uint8_t rangeBytes[] = { 0xD9, 0x40, 0x44, 0x6A, 0x01, 0xD9, 0x5C, 0x24, 0x18 };
        // The range Fantasia used.
        constexpr float throwRange = 30000.0f;
    }

    static float g_diceThrowRange{ throwRange };
    static uintptr_t g_diceThrowReturn{ 0 };

    static __declspec(naked) void hookDiceThrowRange() {
        __asm {
            fld     dword ptr [eax + 0x44]
            push    1
            fstp    dword ptr [esp + 0x18]
            fld     dword ptr [g_diceThrowRange]
            fstp    dword ptr [esp + 0x18]
            jmp     dword ptr [g_diceThrowReturn]
        }
    }

    FarDiceThrow::FarDiceThrow()
        : PatchMod{ "Far Dice Throw", "" },
        m_isEnabled{ false },
        m_isReady{ false },
        m_patch{}
    {
        log("[FarDiceThrow] Entering constructor...");

        auto skill = GetModuleHandleA("Skill.dll");
        auto getSkillUsable = skill != nullptr ? (uintptr_t)GetProcAddress(skill, getSkillUsableName) : 0;

        if (getSkillUsable == 0) {
            log("[FarDiceThrow] Failed to find core::CSkillDiceThrowing::GetSkillUsable");
            log("[FarDiceThrow] Leaving constructor");
            return;
        }

        auto site = getSkillUsable + rangeOffset;

        if (memcmp((const void*)site, rangeBytes, sizeof(rangeBytes)) != 0) {
            log("[FarDiceThrow] The dice throw code at %p isn't the one expected", site);
            log("[FarDiceThrow] Leaving constructor");
            return;
        }

        auto rel = (int32_t)((uintptr_t)&hookDiceThrowRange - (site + 5));

        g_diceThrowReturn = site + sizeof(rangeBytes);
        m_patch.address = site;
        m_patch.bytes = {
            0xE9, (int16_t)(rel & 0xFF), (int16_t)((rel >> 8) & 0xFF), (int16_t)((rel >> 16) & 0xFF), (int16_t)((rel >> 24) & 0xFF),
            0x90, 0x90, 0x90, 0x90
        };
        m_isReady = true;

        log("[FarDiceThrow] Found the dice throw range at %p", site);
        log("[FarDiceThrow] Leaving constructor");
    }

    void FarDiceThrow::apply() {
        if (!m_isReady) {
            return;
        }

        log("[FarDiceThrow] Toggling %s", m_isEnabled ? "on" : "off");

        if (m_isEnabled) {
            patch(m_patch);
        }
        else {
            undoPatch(m_patch);
        }
    }

    void FarDiceThrow::onPatchUI() {
        if (!m_isReady) {
            return;
        }

        if (ImGui::Checkbox("Far Dice Throw", &m_isEnabled)) {
            apply();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Lets you throw dice at a spot much farther away.");
        }
    }

    void FarDiceThrow::onConfigLoad(const Config& cfg) {
        m_isEnabled = cfg.get<bool>("FarDiceThrow.Enabled").value_or(false);

        if (m_isEnabled) {
            apply();
        }
    }

    void FarDiceThrow::onConfigSave(Config& cfg) {
        cfg.set<bool>("FarDiceThrow.Enabled", m_isEnabled);
    }
}
