#include <cstdio>
#include <cstring>
#include <cwchar>

#include <imgui.h>

#include <Scan.hpp>
#include <String.hpp>

#include "Log.hpp"
#include "ShowTrueDurability.hpp"

using namespace std;

namespace kanan {
    using GetColorFn = unsigned long(__thiscall*)(void* item, unsigned long index);
    using GetDurabilityFn = unsigned long(__thiscall*)(void* item);

    static GetColorFn g_getColor{ nullptr };
    static GetDurabilityFn g_getDurability{ nullptr };
    static GetDurabilityFn g_getDurabilityMax{ nullptr };
    // esl::CFormatter::operator<<(unsigned long) and operator<<(const wchar_t*).
    static uintptr_t g_insertNumber{ 0 };
    static uintptr_t g_insertText{ 0 };

    static uintptr_t g_durabilityReturn{ 0 };
    static uintptr_t g_colorReturn{ 0 };
    static uintptr_t g_colorReturnFull{ 0 };

    // The durability text given to the game in place of "code.client.msg.item_durability".
    static wchar_t g_durabilityText[512]{ L"Durability {1}/{3} ({0}/{2})" };
    static wchar_t g_itemColorText[64]{};
    static const wchar_t g_fullDurabilityColor[] = L"<color=3>";

    static const char* DEFAULT_FORMAT = "Durability {1}/{3} ({0}/{2})";

    static void __stdcall formatItemColor(wchar_t* buffer, unsigned long color1, unsigned long color2, unsigned long color3) {
        swprintf_s(buffer, 64, L"%08lX %08lX %08lX", color1, color2, color3);
    }

    // Replaces "formatter << GetInterfaceDurability() << GetInterfaceDurabilityMax()" in the item
    // description with five values: {0} durability, {1} durability x1000, {2} max durability,
    // {3} max durability x1000, {4} item color codes.
    // On entry EDI is the item and ESI the formatter; the game continues with "mov ecx, eax".
    static __declspec(naked) void hookDurabilityText() {
        __asm {
            // {4}
            push    2
            mov     ecx, edi
            call    g_getColor
            push    eax
            push    1
            mov     ecx, edi
            call    g_getColor
            push    eax
            push    0
            mov     ecx, edi
            call    g_getColor
            push    eax
            push    offset g_itemColorText
            call    formatItemColor
            push    offset g_itemColorText

            // {3} and {2}: rounded up like core::IItem::GetInterfaceDurabilityMax.
            mov     ecx, edi
            call    g_getDurabilityMax
            push    eax
            add     eax, 999
            XOR     edx, edx
            mov     ecx, 1000
            div     ecx
            push    eax

            // {1} and {0}: rounded up like core::IItem::GetInterfaceDurability.
            mov     ecx, edi
            call    g_getDurability
            push    eax
            add     eax, 999
            XOR     edx, edx
            mov     ecx, 1000
            div     ecx
            push    eax

            mov     ecx, esi
            call    g_insertNumber
            mov     ecx, eax
            call    g_insertNumber
            mov     ecx, eax
            call    g_insertNumber
            mov     ecx, eax
            call    g_insertNumber
            mov     ecx, eax
            call    g_insertText
            mov     ecx, eax
            jmp     g_durabilityReturn
        }
    }

    // Replaces "call GetInterfaceDurability / cmp eax, 0Ah" where the game decides whether to color
    // the durability line (low durability). Full durability gets its own color.
    // On entry EDI is the item and EBX is core::IItem::GetInterfaceDurability.
    static __declspec(naked) void hookDurabilityColor() {
        __asm {
            mov     ecx, edi
            call    g_getDurabilityMax
            push    eax
            mov     ecx, edi
            call    g_getDurability
            pop     edx
            cmp     eax, edx
            je      fullDurability

            mov     ecx, edi
            call    ebx
            cmp     eax, 0Ah
            jmp     g_colorReturn

        fullDurability:
            push    offset g_fullDurabilityColor
            jmp     g_colorReturnFull
        }
    }

    static Patch makeJump(uintptr_t from, const void* to, size_t length) {
        auto rel = (uintptr_t)to - (from + 5);
        Patch p{};

        p.address = from;
        p.bytes = { 0xE9, (int16_t)(rel & 0xFF), (int16_t)((rel >> 8) & 0xFF), (int16_t)((rel >> 16) & 0xFF), (int16_t)((rel >> 24) & 0xFF) };

        while (p.bytes.size() < length) {
            p.bytes.push_back(0x90);
        }

        return p;
    }

    static uintptr_t callTarget(uintptr_t call) {
        return call + 5 + *(int32_t*)(call + 1);
    }

    ShowTrueDurability::ShowTrueDurability()
        : m_enabled{ false },
        m_showItemColor{ false },
        m_format{},
        m_isAvailable{ false },
        m_patches{},
        m_colorPatches{}
    {
        log("[ShowTrueDurability] Entering constructor...");

        strcpy_s(m_format, DEFAULT_FORMAT);

        auto standard = GetModuleHandleA("Standard.dll");
        auto esl = GetModuleHandleA("ESL.dll");

        if (standard == nullptr || esl == nullptr) {
            log("[ShowTrueDurability] Game modules are not loaded.");
            log("[ShowTrueDurability] Leaving constructor.");
            return;
        }

        g_getColor = (GetColorFn)GetProcAddress(standard, "?GetColor@IItem@core@@QBEKK@Z");
        g_getDurability = (GetDurabilityFn)GetProcAddress(standard, "?GetDurability@IItem@core@@QBEKXZ");
        g_getDurabilityMax = (GetDurabilityFn)GetProcAddress(standard, "?GetDurabilityMax@IItem@core@@QBEKXZ");
        auto getInterfaceDurabilityMax = (uintptr_t)GetProcAddress(standard, "?GetInterfaceDurabilityMax@IItem@core@@QBEKXZ");
        g_insertNumber = (uintptr_t)GetProcAddress(esl, "??6CFormatter@esl@@QAEAAV01@K@Z");
        g_insertText = (uintptr_t)GetProcAddress(esl, "??6CFormatter@esl@@QAEAAV01@PB_W@Z");

        if (g_getColor == nullptr || g_getDurability == nullptr || g_getDurabilityMax == nullptr ||
            getInterfaceDurabilityMax == 0 || g_insertNumber == 0 || g_insertText == 0) {
            log("[ShowTrueDurability] Failed to find the item functions.");
            log("[ShowTrueDurability] Leaving constructor.");
            return;
        }

        // 1. Where the item description formats the durability:
        //    nop / call GetInterfaceDurabilityMax / push eax / mov ecx, edi / call ebx /
        //    mov ecx, esi / mov esi, [CFormatter::operator<<] / push eax / call esi / mov ecx, eax /
        //    call esi / mov ecx, eax
        auto hook = scan("Pleione.dll", "90 E8 ? ? ? ? 50 8B CF FF D3 8B CE 8B 35 ? ? ? ? 50 FF D6 8B C8 FF D6 8B C8");

        if (!hook || callTarget(*hook + 1) != getInterfaceDurabilityMax || **(uintptr_t**)(*hook + 15) != g_insertNumber) {
            log("[ShowTrueDurability] Failed to find the durability text or it has unexpected code.");
            log("[ShowTrueDurability] Leaving constructor.");
            return;
        }

        auto hookSite = *hook + 1;
        g_durabilityReturn = *hook + 26;

        // 2. The durability text: push "code.client.msg.item_durability", a little before the hook.
        auto text = scan(hookSite - 0x80, 0x80, "84 C0 0F 85 ? ? ? ? 68 ? ? ? ? 8D 4D EC 90 E8 ? ? ? ? 8D 45 EC 50 8D 45 D8 50 89 75 FC");
        uintptr_t textOperand{ 0 };

        if (text) {
            textOperand = *text + 9;

            auto key = *(const wchar_t**)textOperand;

            if (key == nullptr || wcscmp(key, L"code.client.msg.item_durability") != 0) {
                textOperand = 0;
            }
        }

        // 3. The low durability color check and the "<color=2>" it uses, after the hook.
        auto color = scan(hookSite, 0x180, "8B CF FF D3 83 F8 0A 0F 87 ? ? ? ? 8B CF 90 E8");
        auto colorText = scan(hookSite, 0x180, "68 ? ? ? ? 8D 4D F0 90 E8 ? ? ? ? FF 75 08 8D 4D EC 51");

        if (textOperand == 0 || !color || !colorText || *colorText < *color) {
            log("[ShowTrueDurability] Failed to find parts of the durability text, not patching.");
            log("[ShowTrueDurability] Leaving constructor.");
            return;
        }

        g_colorReturn = *color + 7;
        g_colorReturnFull = *colorText + 5;

        auto textValue = (uintptr_t)g_durabilityText;
        Patch textPatch{};

        textPatch.address = textOperand;
        textPatch.bytes = { (int16_t)(textValue & 0xFF), (int16_t)((textValue >> 8) & 0xFF), (int16_t)((textValue >> 16) & 0xFF), (int16_t)((textValue >> 24) & 0xFF) };

        m_patches.emplace_back(makeJump(hookSite, &hookDurabilityText, 5));
        m_patches.emplace_back(textPatch);
        m_patches.emplace_back(makeJump(*color + 2, &hookDurabilityColor, 5));
        m_isAvailable = true;

        // 4. The item type check in front of the description: type < 3 skips the durability line
        //    (made to jump to the durability line instead) and so does any type other than 1Bh
        //    after the ranges it handles (nopped).
        //    cmp eax, 3 / jl / cmp eax, 6 / jle / cmp eax, 0Ah / jle / cmp eax, 0Dh / jle /
        //    cmp eax, 1Bh / jne / lea eax, [ebp-...] / push eax / call <durability text>
        auto types = scan("Pleione.dll", "83 F8 03 0F 8C ? ? ? ? 83 F8 06 0F 8E ? ? ? ? 83 F8 0A 7E ? 83 F8 0D 0F 8E ? ? ? ? 83 F8 1B 0F 85 ? ? ? ? 8D 85 ? ? ? ? 50 E8");

        if (types && callTarget(*types + 48) < hookSite && hookSite - callTarget(*types + 48) < 0x100) {
            Patch jumpToDurability{};

            jumpToDurability.address = *types + 3;
            jumpToDurability.bytes = { 0x7C, 0x1E, 0x90, 0x90, 0x90, 0x90 };

            Patch showOtherTypes{};

            showOtherTypes.address = *types + 0x23;
            showOtherTypes.bytes = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };

            m_colorPatches.emplace_back(jumpToDurability);
            m_colorPatches.emplace_back(showOtherTypes);
        }
        else {
            log("[ShowTrueDurability] Failed to find the item type check; color codes will only show on items with durability.");
        }

        log("[ShowTrueDurability] Found the durability text at %p", hookSite);
        log("[ShowTrueDurability] Leaving constructor.");
    }

    void ShowTrueDurability::buildText() {
        auto text = widen(m_format);

        if (m_showItemColor) {
            text += L"</color>\nColor {4}";
        }

        wcsncpy_s(g_durabilityText, text.c_str(), _TRUNCATE);
    }

    void ShowTrueDurability::apply() {
        if (!m_isAvailable) {
            return;
        }

        log("[ShowTrueDurability] Toggling %s (item color %s)", m_enabled ? "on" : "off", m_showItemColor ? "on" : "off");

        buildText();

        for (auto& p : m_patches) {
            if (m_enabled) {
                patch(p);
            }
            else {
                undoPatch(p);
            }
        }

        for (auto& p : m_colorPatches) {
            if (m_enabled && m_showItemColor) {
                patch(p);
            }
            else {
                undoPatch(p);
            }
        }
    }

    void ShowTrueDurability::onUI() {
        if (ImGui::TreeNode("Show True Durability")) {
            if (!m_isAvailable) {
                ImGui::TextWrapped("Not available for this version of the game.");
                ImGui::TreePop();
                return;
            }

            ImGui::TextWrapped(
                "Shows item durability with 1000x precision in item descriptions. "
                "Items at full durability are shown in a different color."
            );
            ImGui::Spacing();

            if (ImGui::Checkbox("Show True Durability", &m_enabled)) {
                apply();
            }

            if (ImGui::Checkbox("Show Item Color Codes", &m_showItemColor)) {
                apply();
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Adds the item's three color codes under the durability, also on items without durability.");
            }

            ImGui::Spacing();
            ImGui::TextWrapped(
                "Durability text: {0} = durability, {1} = durability x1000, "
                "{2} = maximum durability, {3} = maximum durability x1000."
            );

            if (ImGui::InputText("Text", m_format, sizeof(m_format))) {
                buildText();
            }

            if (ImGui::Button("Reset Text")) {
                strcpy_s(m_format, DEFAULT_FORMAT);
                buildText();
            }

            ImGui::TreePop();
        }
    }

    void ShowTrueDurability::onConfigLoad(const Config& cfg) {
        m_enabled = cfg.get<bool>("ShowTrueDurability.Enabled").value_or(false);
        m_showItemColor = cfg.get<bool>("ShowTrueDurability.ShowItemColor").value_or(false);
        strncpy_s(m_format, cfg.get("ShowTrueDurability.Format").value_or(DEFAULT_FORMAT).c_str(), _TRUNCATE);

        if (m_enabled) {
            apply();
        }
    }

    void ShowTrueDurability::onConfigSave(Config& cfg) {
        cfg.set<bool>("ShowTrueDurability.Enabled", m_enabled);
        cfg.set<bool>("ShowTrueDurability.ShowItemColor", m_showItemColor);
        cfg.set("ShowTrueDurability.Format", m_format);
    }
}
