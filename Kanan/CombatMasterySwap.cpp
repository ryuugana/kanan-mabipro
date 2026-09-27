#include <imgui.h>

#include <Scan.hpp>

#include "Log.hpp"
#include "CombatMasterySwap.hpp"

using namespace std;

namespace kanan {
    // Message opcodes seen in the hooks below:
    //   7D00h = combat attack, 6982h = skill prepare, 6986h = skill use,
    //   6987h = skill complete, 6989h = skill cancel.

    // mint::CMessage functions (Mint.dll exports).
    static uintptr_t g_messageCtor{ 0 };     // CMessage::CMessage(unsigned long op, unsigned __int64 id)
    static uintptr_t g_messageWriteU16{ 0 }; // CMessage::WriteU16(unsigned short)

    // Where each hook returns to.
    static uintptr_t g_attackReturn{ 0 };      // after "push 7D00h"
    static uintptr_t g_attackSwapReturn{ 0 };  // after the target id is written to the attack message
    static uintptr_t g_cancelReturn{ 0 };
    static uintptr_t g_completeReturn{ 0 };
    static uintptr_t g_useReturn{ 0 };
    static uintptr_t g_chargeReturn{ 0 };
    static uintptr_t g_prepareReturn{ 0 };

    // State shared with the hooks.
    static int g_isSkillOn{ 0 };     // a skill is loaded (prepared) right now
    static int g_isCharge{ 0 };      // the last skill used was Charge
    static int g_swapSkillID{ 0 };   // the skill to load in place of a normal attack

    // Replaces "push 7D00h" where the game starts building a combat attack message
    // (CMessage at [ebp-30h], attacker id already pushed in edx:eax, target id at [ebp+8]).
    // If no skill is loaded, builds a skill prepare message for the chosen skill instead.
    // Same logic as Fantasia, except Fantasia left 4 extra bytes on the stack here, which the
    // function's epilog would pop into the caller's saved registers; this version doesn't.
    static __declspec(naked) void hookCombatAttack() {
        __asm {
            cmp     edx, 0x00100100             // pets keep attacking normally
            je      normalAttack
            cmp     g_isSkillOn, 0
            jne     clearCharge
            cmp     g_isCharge, 0
            jne     clearCharge

            push    0x6982                      // skill prepare
            lea     ecx, [ebp - 0x30]
            call    g_messageCtor
            AND     dword ptr [ebp - 4], 0      // as the game does after building the message
            push    g_swapSkillID
            lea     ecx, [ebp - 0x30]
            call    g_messageWriteU16           // skill id in place of the target id
            jmp     g_attackSwapReturn

        clearCharge:
            mov     g_isCharge, 0
        normalAttack:
            push    0x7D00                      // replaced instruction
            jmp     g_attackReturn
        }
    }

    // Skill cancel received.
    static __declspec(naked) void hookSkillCancel() {
        __asm {
            mov     g_isSkillOn, 0
            push    0x6989
            jmp     g_cancelReturn
        }
    }

    // Skill completed (core::ISkillMgr::Complete).
    static __declspec(naked) void hookSkillComplete() {
        __asm {
            mov     g_isSkillOn, 0
            push    0x6987
            jmp     g_completeReturn
        }
    }

    // Skill use received.
    static __declspec(naked) void hookSkillUse() {
        __asm {
            mov     g_isSkillOn, 0
            push    0x6986
            jmp     g_useReturn
        }
    }

    // Skill id just read from the skill use message (ax). Charge (4E2Bh) leaves the next attack
    // alone.
    static __declspec(naked) void hookSkillUseCharge() {
        __asm {
            cmp     ax, 0x4E2B
            jne     notCharge
            mov     g_isCharge, 1
        notCharge:
            movzx   edi, ax                     // replaced instructions
            XOR     eax, eax
            jmp     g_chargeReturn
        }
    }

    // Skill prepare received.
    static __declspec(naked) void hookSkillPrepare() {
        __asm {
            mov     g_isSkillOn, 1
            push    0x6982
            jmp     g_prepareReturn
        }
    }

    // A 5 byte JMP from address to destination.
    static Patch makeJmp(uintptr_t address, void* destination) {
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

    CombatMasterySwap::CombatMasterySwap()
        : m_patches{},
        m_isFound{ false },
        m_isEnabled{ false },
        m_skillID{ 0 }
    {
        log("[CombatMasterySwap] Entering constructor...");

        auto mint = GetModuleHandleA("Mint.dll");

        if (mint != nullptr) {
            g_messageCtor = (uintptr_t)GetProcAddress(mint, "??0CMessage@mint@@QAE@K_K@Z");
            g_messageWriteU16 = (uintptr_t)GetProcAddress(mint, "?WriteU16@CMessage@mint@@QAEAAV12@G@Z");
        }

        // Combat attack: push 7D00h; lea ecx, [ebp-30h]; call CMessage::CMessage; push [ebp+0Ch];
        // and [ebp-4], 0; push [ebp+8]; lea ecx, [ebp-30h]; call CMessage::WriteU64; lea eax, [ebp+14h]
        auto attack = scan("Pleione.dll", "68 00 7D 00 00 8D 4D D0 90 E8 ? ? ? ? FF 75 0C 83 65 FC 00 FF 75 08 8D 4D D0 90 E8 ? ? ? ? 8D 45 14");
        // Received skill cancel: push 6989h
        auto cancel = scan("Pleione.dll", "68 89 69 00 00 E8 ? ? ? ? 8D 45 08 50 8D 4D");
        // core::ISkillMgr::Complete: push 6987h
        auto complete = scan("Standard.dll", "68 87 69 00 00 8D 4D 0C");
        // Received skill use: push 6986h; call; lea ecx, [ebp+8]; call CMessage::ReadU16; movzx edi, ax; xor eax, eax
        auto use = scan("Pleione.dll", "68 86 69 00 00 E8 ? ? ? ? 8D 4D 08 90 E8 ? ? ? ? 0F B7 F8 33 C0");
        // Received skill prepare: push 6982h
        auto prepare = scan("Pleione.dll", "68 82 69 00 00 E8 ? ? ? ? 8D 4D 08 90 E8 ? ? ? ? 53");

        if (g_messageCtor == 0 || g_messageWriteU16 == 0 || !attack || !cancel || !complete || !use || !prepare) {
            log("[CombatMasterySwap] Failed to find everything needed (message %d, attack %d, cancel %d, complete %d, use %d, prepare %d)",
                g_messageCtor != 0 && g_messageWriteU16 != 0, (bool)attack, (bool)cancel, (bool)complete, (bool)use, (bool)prepare);
            log("[CombatMasterySwap] Leaving constructor");
            return;
        }

        g_attackReturn = *attack + 5;
        g_attackSwapReturn = *attack + 0x21;
        g_cancelReturn = *cancel + 5;
        g_completeReturn = *complete + 5;
        g_useReturn = *use + 5;
        g_chargeReturn = *use + 0x13 + 5;
        g_prepareReturn = *prepare + 5;

        m_patches.emplace_back(makeJmp(*attack, &hookCombatAttack));
        m_patches.emplace_back(makeJmp(*cancel, &hookSkillCancel));
        m_patches.emplace_back(makeJmp(*complete, &hookSkillComplete));
        m_patches.emplace_back(makeJmp(*use, &hookSkillUse));
        m_patches.emplace_back(makeJmp(*use + 0x13, &hookSkillUseCharge));
        m_patches.emplace_back(makeJmp(*prepare, &hookSkillPrepare));

        m_isFound = true;

        log("[CombatMasterySwap] Found attack %p, cancel %p, complete %p, use %p, prepare %p",
            *attack, *cancel, *complete, *use, *prepare);
        log("[CombatMasterySwap] Leaving constructor");
    }

    void CombatMasterySwap::onUI() {
        if (!m_isFound) {
            return;
        }

        if (ImGui::TreeNode("Combat Mastery Swap")) {
            ImGui::TextWrapped("When you attack with no skill loaded, loads the skill below instead "
                "(for example 20002 for Smash). Once it is loaded, your next attack uses it.");
            ImGui::Spacing();

            if (ImGui::Checkbox("Enable Combat Mastery Swap", &m_isEnabled)) {
                apply();
            }

            if (ImGui::InputInt("Skill ID", &m_skillID)) {
                if (m_skillID < 0) {
                    m_skillID = 0;
                }
                else if (m_skillID > 0xFFFF) {
                    m_skillID = 0xFFFF;
                }

                g_swapSkillID = m_skillID;
                apply();
            }

            ImGui::TreePop();
        }
    }

    void CombatMasterySwap::onConfigLoad(const Config& cfg) {
        m_isEnabled = cfg.get<bool>("CombatMasterySwap.Enabled").value_or(false);
        m_skillID = cfg.get<int>("CombatMasterySwap.SkillID").value_or(0);

        if (m_skillID < 0 || m_skillID > 0xFFFF) {
            m_skillID = 0;
        }

        g_swapSkillID = m_skillID;

        if (m_isEnabled) {
            apply();
        }
    }

    void CombatMasterySwap::onConfigSave(Config& cfg) {
        cfg.set<bool>("CombatMasterySwap.Enabled", m_isEnabled);
        cfg.set<int>("CombatMasterySwap.SkillID", m_skillID);
    }

    void CombatMasterySwap::apply() {
        if (!m_isFound) {
            return;
        }

        // Like Fantasia: on only with a skill id, and the tracked state starts fresh.
        auto on = m_isEnabled && m_skillID != 0;

        g_isSkillOn = 0;
        g_isCharge = 0;

        log("[CombatMasterySwap] Toggling %s (skill %d)", on ? "on" : "off", m_skillID);

        for (auto& p : m_patches) {
            if (on) {
                patch(p);
            }
            else {
                undoPatch(p);
            }
        }
    }
}
