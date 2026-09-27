#include <imgui.h>

#include <Scan.hpp>

#include "Log.hpp"
#include "DefaultRangedSwap.hpp"

using namespace std;

namespace kanan {
    // Skill ids, in the same order as Fantasia's DefaultRangedSwap setting (0-6).
    static const uint16_t g_rangedSkills[] = {
        0x5209, // 0: Ranged Attack (21001, the game's default)
        0x520A, // 1: Magnum Shot (21002)
        0x520B, // 2: Mari's Arrow Revolver (21003)
        0x520C, // 3: Arrow Revolver (21004)
        0x520E, // 4: Support Shot (21006)
        0x520F, // 5: Mirage Missile (21007)
        0x55FB, // 6: Crash Shot (22011)
    };

    DefaultRangedSwap::DefaultRangedSwap()
        : PatchMod{ "Default Ranged Swap", "" },
        m_patch{},
        m_choice{ 0 }
    {
        // End of core::ISkillMgr::GetBasicRangedAttackSkill:
        //   test al, al
        //   mov eax, 5212h
        //   jne done
        //   add eax, -9          ; 5209h = Ranged Attack
        // done:
        //   ret
        auto address = scan("Standard.dll", "84 C0 B8 12 52 00 00 75 03 83 C0 F7 C3");

        if (address) {
            log("[DefaultRangedSwap] Found address %p", *address + 2);

            m_patch.address = *address + 2;
        }
        else {
            log("[DefaultRangedSwap] Failed to find address.");
        }
    }

    void DefaultRangedSwap::onPatchUI() {
        if (m_patch.address == 0) {
            return;
        }

        if (ImGui::Combo("Default Ranged Swap", &m_choice, "Ranged Attack (off)\0Magnum Shot\0Mari's Arrow Revolver\0Arrow Revolver\0Support Shot\0Mirage Missile\0Crash Shot\0\0")) {
            apply();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Uses the chosen skill in place of the basic Ranged Attack.");
        }
    }

    void DefaultRangedSwap::onConfigLoad(const Config& cfg) {
        m_choice = cfg.get<int>("DefaultRangedSwap.Choice").value_or(0);

        if (m_choice < 0 || m_choice > 6) {
            m_choice = 0;
        }

        if (m_choice != 0) {
            apply();
        }
    }

    void DefaultRangedSwap::onConfigSave(Config& cfg) {
        cfg.set<int>("DefaultRangedSwap.Choice", m_choice);
    }

    void DefaultRangedSwap::apply() {
        if (m_patch.address == 0) {
            return;
        }

        log("[DefaultRangedSwap] Applying choice %d", m_choice);

        if (m_choice <= 0 || m_choice > 6) {
            undoPatch(m_patch);
            return;
        }

        // Same result as Fantasia's hook, which only sets AX (every caller reads AX only):
        //   mov ax, 5212h
        //   jne done
        //   mov ax, <skill>
        // done:
        auto skill = g_rangedSkills[m_choice];

        m_patch.bytes = {
            0x66, 0xB8, 0x12, 0x52,
            0x75, 0x04,
            0x66, 0xB8, (int16_t)(skill & 0xFF), (int16_t)(skill >> 8)
        };

        patch(m_patch);
    }
}
