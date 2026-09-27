#include <cstdio>
#include <cwchar>

#include <imgui.h>

#include <Scan.hpp>

#include "Log.hpp"
#include "ShowPoisonDurability.hpp"

namespace kanan {
    using GetPoisonDurabilityFn = unsigned long(__thiscall*)(void* item);

    static GetPoisonDurabilityFn g_getPoisonDurability{ nullptr };
    static uintptr_t g_getItemPoison{ 0 };
    static uintptr_t g_isPoisonedReturn{ 0 };
    static wchar_t g_poisonText[256]{ L"</color>" };

    static void __cdecl formatPoisonText(unsigned long durability) {
        swprintf_s(g_poisonText, L"\n\nPoison Durability: %lu/100</color>", durability);
    }

    // Replaces the call to IItem::GetItemPoison at the start of IItem::IsPoisoned. Before doing
    // what the game did, it remembers the item's poison durability as the text that the item
    // description adds after the poison line.
    static __declspec(naked) void hookIsPoisoned() {
        __asm {
            push    ecx
            call    g_getPoisonDurability
            push    eax
            call    formatPoisonText
            add     esp, 4
            pop     ecx
            call    g_getItemPoison
            jmp     g_isPoisonedReturn
        }
    }

    static Patch makePointerPatch(uintptr_t address, const void* pointer) {
        auto value = (uintptr_t)pointer;
        Patch p{};

        p.address = address;
        p.bytes = { (int16_t)(value & 0xFF), (int16_t)((value >> 8) & 0xFF), (int16_t)((value >> 16) & 0xFF), (int16_t)((value >> 24) & 0xFF) };

        return p;
    }

    ShowPoisonDurability::ShowPoisonDurability()
        : PatchMod{ "Show Poison Durability", "Shows how much poison durability is left on poisoned items." },
        m_enabled{ false },
        m_isAvailable{ false },
        m_patches{}
    {
        log("[ShowPoisonDurability] Entering constructor...");

        auto standard = GetModuleHandleA("Standard.dll");
        auto isPoisoned = standard ? (uintptr_t)GetProcAddress(standard, "?IsPoisoned@IItem@core@@QBE_NXZ") : 0;
        auto getItemPoison = standard ? (uintptr_t)GetProcAddress(standard, "?GetItemPoison@IItem@core@@QBEGXZ") : 0;
        g_getPoisonDurability = standard ? (GetPoisonDurabilityFn)GetProcAddress(standard, "?GetItemPotionPoisonDurability@IItem@core@@QBEKXZ") : nullptr;

        // IsPoisoned must start with "call GetItemPoison".
        if (isPoisoned == 0 || getItemPoison == 0 || g_getPoisonDurability == nullptr ||
            *(uint8_t*)isPoisoned != 0xE8 || isPoisoned + 5 + *(int32_t*)(isPoisoned + 1) != getItemPoison) {
            log("[ShowPoisonDurability] Failed to find IItem::IsPoisoned or it has unexpected code.");
            log("[ShowPoisonDurability] Leaving constructor.");
            return;
        }

        g_getItemPoison = getItemPoison;
        g_isPoisonedReturn = isPoisoned + 5;

        auto rel = (uintptr_t)&hookIsPoisoned - (isPoisoned + 5);
        Patch hook{};

        hook.address = isPoisoned;
        hook.bytes = { 0xE9, (int16_t)(rel & 0xFF), (int16_t)((rel >> 8) & 0xFF), (int16_t)((rel >> 16) & 0xFF), (int16_t)((rel >> 24) & 0xFF) };
        m_patches.emplace_back(hook);

        // The item description adds "</color>" right after the poison line. There are two copies of
        // this code (AstralWorld patched only the second one); patch both.
        //   test al, al / je +2B / mov ecx, [ebp+70h] / push "<color=6>" / call += /
        //   mov ecx, [ebp+70h] / lea eax, [ebp+54h] / push eax / call += / mov ecx, [ebp+70h] / push "</color>"
        const char* pattern = "84 C0 74 2B 8B 4D 70 68 ? ? ? ? 90 E8 ? ? ? ? 8B 4D 70 8D 45 54 50 90 E8 ? ? ? ? 8B 4D 70 68";
        auto address = scan("Pleione.dll", pattern);

        while (address) {
            auto push = *address + 34;
            auto text = *(const wchar_t**)(push + 1);

            if (*(uint8_t*)push == 0x68 && text != nullptr && wcscmp(text, L"</color>") == 0) {
                m_patches.emplace_back(makePointerPatch(push + 1, g_poisonText));
                log("[ShowPoisonDurability] Found an item description at %p", push);
            }
            else {
                log("[ShowPoisonDurability] Unexpected code at %p, skipping it.", push);
            }

            address = scan("Pleione.dll", *address + 1, pattern);
        }

        m_isAvailable = m_patches.size() > 1;

        if (!m_isAvailable) {
            log("[ShowPoisonDurability] Failed to find the item description.");
        }

        log("[ShowPoisonDurability] Leaving constructor.");
    }

    void ShowPoisonDurability::apply() {
        if (!m_isAvailable) {
            return;
        }

        log("[ShowPoisonDurability] Toggling %s", m_enabled ? "on" : "off");

        for (auto& p : m_patches) {
            if (m_enabled) {
                patch(p);
            }
            else {
                undoPatch(p);
            }
        }
    }

    void ShowPoisonDurability::onPatchUI() {
        if (!m_isAvailable) {
            return;
        }

        if (ImGui::Checkbox("Show Poison Durability", &m_enabled)) {
            apply();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows how much poison durability is left on poisoned items.");
        }
    }

    void ShowPoisonDurability::onConfigLoad(const Config& cfg) {
        m_enabled = cfg.get<bool>("ShowPoisonDurability.Enabled").value_or(false);

        if (m_enabled) {
            apply();
        }
    }

    void ShowPoisonDurability::onConfigSave(Config& cfg) {
        cfg.set<bool>("ShowPoisonDurability.Enabled", m_enabled);
    }
}
