#include <cwchar>

#include <imgui.h>

#include <Scan.hpp>

#include "Log.hpp"
#include "ShowCombatPower.hpp"

namespace kanan {
    //
    // How the game builds the text above a character's name (Pleione.dll):
    //
    //   Name tag builder:   IsNPC(character)? -> JZ skip  -> CALL buildRankPrefix
    //   buildRankPrefix:    JZ    (not an NPC)            -> plain name
    //                       JNZ   (IsNamedNPC)            -> plain name
    //                       JNZ   (virtual check +208h)   -> plain name
    //                       CStringT prefix;  switch (GetTargetCombatPower) {
    //                           WEAKEST/WEAK/STRONG/AWFUL/BOSS: PUSH L"<mini>...</mini>"; prefix = that
    //                           default (same level):           no prefix }
    //                       name = prefix + GetDisplayName()
    //
    // Like Fantasia, we let every character through, send "no rank" cases to combatPowerNoRank and
    // replace "prefix = <rank text>" with combatPowerText, which appends the numbers.
    //

    static wchar_t g_emptyPrefix[1]{ L"" };
    static wchar_t g_nameBuffer[256]{};

    static uintptr_t g_cstringConstruct{ 0 };  // esl::CStringT<wchar_t>::CStringT(void)
    static uintptr_t g_cstringAssign{ 0 };     // esl::CStringT<wchar_t>::operator=(wchar_t const*)
    static uintptr_t g_getCombatPower{ 0 };    // core::IParameterBase2::GetCombatPower() const -> float
    static uintptr_t g_returnAddress{ 0 };     // back into buildRankPrefix, after "prefix = <rank text>"

    static void __stdcall formatCombatPower(wchar_t* buffer, double combatPower, const wchar_t* prefix) {
        // Same text as Fantasia. An empty prefix means the game shows no rank (same level, players, named NPCs).
        bool noRank = prefix == nullptr || prefix[0] == L'\0';

        if (noRank) {
            swprintf_s(buffer, 256, L"<mini>EVEN</mini> %.2f ", combatPower);
        }
        else {
            swprintf_s(buffer, 256, L"%s%.2f ", prefix, combatPower);
        }
    }

    // Entered in place of "prefix = <rank text>". The rank text (or an empty string) is on the stack
    // and is consumed by formatCombatPower. ESI = the character, [EBP-10h] = the prefix CStringT.
    __declspec(naked) static void combatPowerText() {
        __asm {
            mov     ecx, esi
            mov     eax, [ecx]
            call    dword ptr [eax + 4Ch]       // characters parameters (same call Fantasia uses)
            mov     ecx, eax
            call    g_getCombatPower
            sub     esp, 8
            fstp    qword ptr [esp]             // combatPower
            push    offset g_nameBuffer
            call    formatCombatPower           // (buffer, combatPower, prefix), cleans its arguments

            push    offset g_nameBuffer
            lea     ecx, [ebp - 10h]
            call    g_cstringAssign             // prefix = our text
            jmp     g_returnAddress
        }
    }

    // Entered for characters the game shows without a rank. Sets up the prefix CStringT the way the
    // game does, then continues with an empty rank text.
    __declspec(naked) static void combatPowerNoRank() {
        __asm {
            lea     ecx, [ebp - 10h]
            call    g_cstringConstruct
            AND     dword ptr [ebp - 4], 0
            push    offset g_emptyPrefix
            jmp     combatPowerText
        }
    }

    static uintptr_t callTarget(uintptr_t call) {
        return call + 5 + *(int32_t*)(call + 1);
    }

    static void addRel32(std::vector<int16_t>& bytes, uintptr_t nextInstruction, uintptr_t target) {
        auto rel = (uint32_t)(target - nextInstruction);

        for (int i = 0; i < 4; ++i) {
            bytes.push_back((int16_t)((rel >> (i * 8)) & 0xFF));
        }
    }

    ShowCombatPower::ShowCombatPower()
        : PatchMod{ "Show Combat Power", "" },
        m_showCombatPower{ false },
        m_isReady{ false },
        m_patches{}
    {
        auto esl = GetModuleHandleA("ESL.dll");
        auto standard = GetModuleHandleA("Standard.dll");

        g_cstringConstruct = (uintptr_t)GetProcAddress(esl, "??0?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QAE@XZ");
        g_cstringAssign = (uintptr_t)GetProcAddress(esl, "??4?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QAEAAV01@PB_W@Z");
        g_getCombatPower = (uintptr_t)GetProcAddress(standard, "?GetCombatPower@IParameterBase2@core@@QBEMXZ");

        if (g_cstringConstruct == 0 || g_cstringAssign == 0 || g_getCombatPower == 0) {
            log("[ShowCombatPower] Failed to find the game functions it needs.");
            return;
        }

        // Name tag builder: TEST AL, AL / JZ +1Ah (skip if not an NPC) / LEA EAX, [EBP+8] / PUSH EAX / CALL buildRankPrefix
        auto caller = scan("Pleione.dll", "84 C0 74 1A 8D 45 08 50 E8 ? ? ? ? 8B 16 83 65 FC 00 50 8B CE FF 52 34");

        // buildRankPrefix+31h: the three "show the plain name" checks.
        auto checks = scan("Pleione.dll", "0F 84 AD 00 00 00 8B CE 90 E8 ? ? ? ? 84 C0 0F 85 9D 00 00 00 8B 06 8B CE FF 90 08 02 00 00 84 C0 0F 85 8B 00 00 00");

        // DEC EAX / JNZ +2Ah (no rank) / PUSH L"<mini>BOSS </mini>" / JMP / PUSH L"<mini>AWFUL </mini>" / JMP / PUSH ...
        auto rank = scan("Pleione.dll", "48 75 2A 68 ? ? ? ? EB 1A 68 ? ? ? ? EB 13 68");

        // PUSH L"<mini>WEAKEST </mini>" / LEA ECX, [EBP-10h] / CALL CStringT::operator= / PUSH 1 / PUSH 1 / ...
        auto text = scan("Pleione.dll", "68 ? ? ? ? 8D 4D F0 90 E8 ? ? ? ? 6A 01 6A 01 8D 45 EC 50 8B CE");

        if (!caller || !checks || !rank || !text) {
            log("[ShowCombatPower] Failed to find the name tag code (%p %p %p %p).",
                caller.value_or(0), checks.value_or(0), rank.value_or(0), text.value_or(0));
            return;
        }

        auto checksAddress = *checks;
        auto rankAddress = *rank + 1;       // JNZ +2Ah
        auto textAddress = *text + 5;       // LEA ECX, [EBP-10h] / NOP / CALL operator=

        // Make sure all four places belong to the same function and use the functions we resolved.
        auto buildRankPrefix = callTarget(*caller + 8);
        bool isSane = checksAddress - buildRankPrefix == 0x31 &&
            rankAddress > checksAddress && rankAddress - checksAddress < 0x80 &&
            textAddress > rankAddress && textAddress - rankAddress < 0x40 &&
            callTarget(checksAddress + 0x2C) == g_cstringConstruct &&
            callTarget(textAddress + 4) == g_cstringAssign;

        auto rel8 = (intptr_t)(checksAddress + 0x22) - (intptr_t)(rankAddress + 2);

        if (!isSane || rel8 < -128 || rel8 > 127) {
            log("[ShowCombatPower] Name tag code is not laid out as expected, not patching.");
            return;
        }

        log("[ShowCombatPower] Found name tag code at %p, %p, %p, %p", *caller, checksAddress, rankAddress, textAddress);

        g_returnAddress = textAddress + 9;

        Patch p{};

        // Name tag builder: build the rank prefix for every character, not just NPCs.
        p.address = *caller + 2;
        p.bytes = { 0x90, 0x90 };
        m_patches.emplace_back(p);

        // Don't bail out for non-NPCs or named NPCs.
        p.address = checksAddress;
        p.bytes = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
        m_patches.emplace_back(p);

        p.address = checksAddress + 0x10;
        p.bytes = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
        m_patches.emplace_back(p);

        // Third check: JNZ combatPowerNoRank instead of the plain name.
        p.address = checksAddress + 0x22;
        p.bytes = { 0x0F, 0x85 };
        addRel32(p.bytes, checksAddress + 0x28, (uintptr_t)&combatPowerNoRank);
        m_patches.emplace_back(p);

        // No rank (same level): jump to that JNZ (flags are still "not zero" here).
        p.address = rankAddress;
        p.bytes = { -1, (int16_t)(rel8 & 0xFF) };
        m_patches.emplace_back(p);

        // "prefix = <rank text>" -> JMP combatPowerText.
        p.address = textAddress;
        p.bytes = { 0xE9 };
        addRel32(p.bytes, textAddress + 5, (uintptr_t)&combatPowerText);
        p.bytes.insert(p.bytes.end(), { 0x90, 0x90, 0x90, 0x90 });
        m_patches.emplace_back(p);

        m_isReady = true;
    }

    void ShowCombatPower::apply() {
        if (!m_isReady) {
            return;
        }

        if (m_showCombatPower) {
            log("[ShowCombatPower] Toggling on");

            for (auto& p : m_patches) {
                patch(p);
            }
        }
        else {
            log("[ShowCombatPower] Toggling off");

            for (auto& p : m_patches) {
                undoPatch(p);
            }
        }
    }

    void ShowCombatPower::onPatchUI() {
        if (!m_isReady) {
            return;
        }

        if (ImGui::Checkbox("Show Combat Power", &m_showCombatPower)) {
            apply();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows the combat power number next to character names.");
        }
    }

    void ShowCombatPower::onConfigLoad(const Config& cfg) {
        m_showCombatPower = cfg.get<bool>("ShowCombatPower.CombatPower").value_or(false);

        if (m_showCombatPower) {
            apply();
        }
    }

    void ShowCombatPower::onConfigSave(Config& cfg) {
        cfg.set<bool>("ShowCombatPower.CombatPower", m_showCombatPower);
    }
}
