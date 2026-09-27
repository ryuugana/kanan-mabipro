#include <cwchar>

#include <imgui.h>

#include <Scan.hpp>

#include "Log.hpp"
#include "ShowExplorationPercent.hpp"

namespace kanan {
    //
    // Character window, exploration level line (Pleione.dll):
    //
    //   +00  PUSH L"code.interface.window.character.explo_level"   <- we push our own text instead
    //        key = CStringT(that)
    //        formatter = CLocalizer::Instance().GetLocalText(key)  (stored in [EBP-18h])
    //   +37  CALL IParameterBase2::GetExploLevel                   <- we call explorationPercent instead
    //        formatter << level
    //
    // As in Fantasia, the localizer gives back unknown text as-is, so our text becomes the format:
    // {0} is the percentage we insert first, {1} is the level the game inserts afterwards.
    //
    static wchar_t g_explorationText[]{ L"Expl Lv{1} {0}%" };
    static wchar_t g_percentText[16]{};

    static uintptr_t g_getExploLevel{ 0 };        // core::IParameterBase2::GetExploLevel() const
    static uintptr_t g_getExploExpPercent{ 0 };   // core::IParameter::GetExploExpPercent() const -> float
    static uintptr_t g_formatterInsertText{ 0 };  // esl::CFormatter::operator<<(wchar_t const*)

    static void __stdcall formatPercent(wchar_t* buffer, float percent) {
        swprintf_s(buffer, 16, L"%.1f", percent * 100.0);
    }

    // Called in place of GetExploLevel with ECX = the character's parameters.
    __declspec(naked) static void explorationPercent() {
        __asm {
            push    ecx
            call    g_getExploExpPercent
            sub     esp, 4
            fstp    dword ptr [esp]
            push    offset g_percentText
            call    formatPercent               // (buffer, percent), cleans its arguments

            mov     ecx, [ebp - 18h]            // the CFormatter from GetLocalText
            push    offset g_percentText
            call    g_formatterInsertText
            mov     [ebp - 18h], eax

            pop     ecx
            jmp     g_getExploLevel             // let the game get the level as usual
        }
    }

    static uintptr_t callTarget(uintptr_t call) {
        return call + 5 + *(int32_t*)(call + 1);
    }

    static void addDword(std::vector<int16_t>& bytes, uint32_t value) {
        for (int i = 0; i < 4; ++i) {
            bytes.push_back((int16_t)((value >> (i * 8)) & 0xFF));
        }
    }

    ShowExplorationPercent::ShowExplorationPercent()
        : PatchMod{ "Show Exploration Percent", "" },
        m_isEnabled{ false },
        m_textPatch{},
        m_callPatch{}
    {
        auto esl = GetModuleHandleA("ESL.dll");
        auto standard = GetModuleHandleA("Standard.dll");

        g_getExploLevel = (uintptr_t)GetProcAddress(standard, "?GetExploLevel@IParameterBase2@core@@QBEGXZ");
        g_getExploExpPercent = (uintptr_t)GetProcAddress(standard, "?GetExploExpPercent@IParameter@core@@QBEMXZ");
        g_formatterInsertText = (uintptr_t)GetProcAddress(esl, "??6CFormatter@esl@@QAEAAV01@PB_W@Z");

        if (g_getExploLevel == 0 || g_getExploExpPercent == 0 || g_formatterInsertText == 0) {
            log("[ShowExplorationPercent] Failed to find the game functions it needs.");
            return;
        }

        auto address = scan("Pleione.dll",
            "68 ? ? ? ? 8D 4D EC 90 E8 ? ? ? ? 8D 45 EC 50 8D 45 B8 50 C6 45 FC 46 90 E8 ? ? ? ? "
            "8B C8 90 E8 ? ? ? ? 89 45 E8 8B 4D DC 8D 45 E4 50 C6 45 FC 47 90 E8");

        if (!address) {
            log("[ShowExplorationPercent] Failed to find the exploration level text.");
            return;
        }

        auto keyText = *(const wchar_t**)(*address + 1);
        auto levelCall = *address + 0x37;

        if (wcscmp(keyText, L"code.interface.window.character.explo_level") != 0 ||
            callTarget(levelCall) != g_getExploLevel) {
            log("[ShowExplorationPercent] Exploration level text is not as expected, not patching.");
            return;
        }

        log("[ShowExplorationPercent] Found exploration level text at %p", *address);

        // PUSH OFFSET g_explorationText
        m_textPatch.address = *address + 1;
        addDword(m_textPatch.bytes, (uint32_t)(uintptr_t)&g_explorationText);

        // CALL explorationPercent
        m_callPatch.address = levelCall + 1;
        addDword(m_callPatch.bytes, (uint32_t)((uintptr_t)&explorationPercent - (levelCall + 5)));
    }

    void ShowExplorationPercent::apply() {
        if (m_textPatch.address == 0) {
            return;
        }

        log("[ShowExplorationPercent] Toggling %s", m_isEnabled ? "on" : "off");

        if (m_isEnabled) {
            patch(m_callPatch);
            patch(m_textPatch);
        }
        else {
            undoPatch(m_textPatch);
            undoPatch(m_callPatch);
        }
    }

    void ShowExplorationPercent::onPatchUI() {
        if (m_textPatch.address == 0) {
            return;
        }

        if (ImGui::Checkbox("Show Exploration Percent", &m_isEnabled)) {
            apply();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows how far you are towards your next exploration level in the character window.");
        }
    }

    void ShowExplorationPercent::onConfigLoad(const Config& cfg) {
        m_isEnabled = cfg.get<bool>("ShowExplorationPercent.Enabled").value_or(false);

        if (m_isEnabled) {
            apply();
        }
    }

    void ShowExplorationPercent::onConfigSave(Config& cfg) {
        cfg.set<bool>("ShowExplorationPercent.Enabled", m_isEnabled);
    }
}
