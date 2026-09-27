#include <cwchar>

#include <imgui.h>

#include <Scan.hpp>

#include "Log.hpp"
#include "ShowItemID.hpp"

namespace kanan {
    //
    // Item description builder (Pleione.dll, ECX = core::IItem):
    //
    //   +000  PUSH EBP / SUB ESP, 64h / PUSH 20h         <- JMP itemTooltipEntry (remembers the item ID)
    //   ...
    //   +520  MOV ECX, [EBP+70h]
    //         PUSH L"<color=1>"                          <- JMP itemTooltipText (pushes "\nItem ID: n<color=1>")
    //         CALL CStringT::operator+=
    //
    // The item ID is the value core::IItem::GetDBClassId returns ([item+0Ch]).
    //
    static uint32_t g_itemId{ 0 };
    static const wchar_t* g_originalText{ nullptr };
    static wchar_t g_itemText[64]{};

    static uintptr_t g_entryReturn{ 0 };
    static uintptr_t g_textReturn{ 0 };

    static void __stdcall formatItemId() {
        swprintf_s(g_itemText, L"\nItem ID: %u%s", g_itemId, g_originalText);
    }

    __declspec(naked) static void itemTooltipEntry() {
        __asm {
            mov     eax, [ecx + 0Ch]            // IItem::GetDBClassId
            mov     g_itemId, eax

            push    ebp                         // the instructions we replaced
            sub     esp, 64h
            push    20h
            jmp     g_entryReturn
        }
    }

    __declspec(naked) static void itemTooltipText() {
        __asm {
            push    ecx                         // ECX is already set up for the CStringT::operator+= call
            push    edx
            push    eax
            call    formatItemId
            pop     eax
            pop     edx
            pop     ecx

            push    offset g_itemText           // instead of L<color=1>
            jmp     g_textReturn
        }
    }

    static void addDword(std::vector<int16_t>& bytes, uint32_t value) {
        for (int i = 0; i < 4; ++i) {
            bytes.push_back((int16_t)((value >> (i * 8)) & 0xFF));
        }
    }

    ShowItemID::ShowItemID()
        : PatchMod{ "Show Item ID", "" },
        m_isEnabled{ false },
        m_entryPatch{},
        m_textPatch{}
    {
        // RET 0Ch (end of previous function) / PUSH EBP / SUB ESP, 64h / PUSH 20h / MOV EAX, ... / CALL ... /
        // MOV ESI, ECX / MOV [EBP+64h], ESI / AND [EBP+58h], 0 / PUSH [EBP+78h]
        auto entry = scan("Pleione.dll", "C2 0C 00 55 83 EC 64 6A 20 B8 ? ? ? ? E8 ? ? ? ? 8B F1 89 75 64 83 65 58 00 FF 75 78");

        // CALL ESI / MOV ECX, [EBP+70h] / PUSH L"<color=1>" / NOP / CALL CStringT::operator+= / PUSH [EBP+70h]
        auto text = scan("Pleione.dll", "FF D6 8B 4D 70 68 ? ? ? ? 90 E8 ? ? ? ? FF 75 70");

        if (!entry || !text) {
            log("[ShowItemID] Failed to find the item description code (%p %p).", entry.value_or(0), text.value_or(0));
            return;
        }

        auto entryAddress = *entry + 3;
        auto textAddress = *text + 5;

        g_originalText = *(const wchar_t**)(textAddress + 1);

        if (textAddress < entryAddress || textAddress - entryAddress > 0x1000 ||
            wcscmp(g_originalText, L"<color=1>") != 0) {
            log("[ShowItemID] Item description code is not as expected, not patching.");
            return;
        }

        log("[ShowItemID] Found item description code at %p and %p", entryAddress, textAddress);

        g_entryReturn = entryAddress + 6;
        g_textReturn = textAddress + 5;

        // JMP itemTooltipEntry / NOP
        m_entryPatch.address = entryAddress;
        m_entryPatch.bytes = { 0xE9 };
        addDword(m_entryPatch.bytes, (uint32_t)((uintptr_t)&itemTooltipEntry - (entryAddress + 5)));
        m_entryPatch.bytes.push_back(0x90);

        // JMP itemTooltipText
        m_textPatch.address = textAddress;
        m_textPatch.bytes = { 0xE9 };
        addDword(m_textPatch.bytes, (uint32_t)((uintptr_t)&itemTooltipText - (textAddress + 5)));
    }

    void ShowItemID::apply() {
        if (m_entryPatch.address == 0) {
            return;
        }

        log("[ShowItemID] Toggling %s", m_isEnabled ? "on" : "off");

        if (m_isEnabled) {
            patch(m_entryPatch);
            patch(m_textPatch);
        }
        else {
            undoPatch(m_textPatch);
            undoPatch(m_entryPatch);
        }
    }

    void ShowItemID::onPatchUI() {
        if (m_entryPatch.address == 0) {
            return;
        }

        if (ImGui::Checkbox("Show Item ID", &m_isEnabled)) {
            apply();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows the item's ID number in item descriptions.");
        }
    }

    void ShowItemID::onConfigLoad(const Config& cfg) {
        m_isEnabled = cfg.get<bool>("ShowItemID.Enabled").value_or(false);

        if (m_isEnabled) {
            apply();
        }
    }

    void ShowItemID::onConfigSave(Config& cfg) {
        cfg.set<bool>("ShowItemID.Enabled", m_isEnabled);
    }
}
