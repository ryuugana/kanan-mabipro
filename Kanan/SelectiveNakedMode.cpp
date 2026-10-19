#include <Windows.h>
#include <cstring>

#include <FunctionHook.hpp>
#include <Patch.hpp>
#include <imgui.h>

#include "Log.hpp"
#include "SelectiveNakedMode.hpp"

using namespace std;

namespace kanan {

    // Trampoline addresses for original function prologues.
    static uintptr_t g_trampolineAddr{ 0 };           // FUN_638e9416 (body render)
    static uintptr_t g_clothingTrampolineAddr{ 0 };   // FUN_638e9b58 (clothing render)

    // Last pocket index seen by each naked hook — for diagnostics only.
    static int g_lastPocketIndex{ -1 };
    static int g_lastClothingPocketIndex{ -1 };

    // Circular buffer of per-call pocket diagnostics, written from __asm.
    struct PocketCallEntry {
        int pocketIndex;
        bool wentNaked;
    };
    static PocketCallEntry g_pocketLog[64];
    static int g_pocketLogCount{ 0 };
    static PocketCallEntry g_clothingLog[64];
    static int g_clothingLogCount{ 0 };

    // Called from the naked hook to log each pocket render call.
    // __cdecl so the caller (asm) cleans up the stack.
    static void __cdecl logPocketCall(int pocketIndex, bool wentNaked)
    {
        g_pocketLog[g_pocketLogCount & 63] = { pocketIndex, wentNaked };
        g_pocketLogCount++;
        log("[SelectiveNakedMode] Pocket %d: %s", pocketIndex, wentNaked ? "NAKED" : "CLOTHED");
    }

    // Called from the clothing naked hook to log each clothing render call.
    static void __cdecl logClothingPocketCall(int pocketIndex, bool wentNaked)
    {
        g_clothingLog[g_clothingLogCount & 63] = { pocketIndex, wentNaked };
        g_clothingLogCount++;
        log("[SelectiveNakedModeClothing] Pocket %d: %s", pocketIndex, wentNaked ? "NAKED" : "CLOTHED");
    }

    // Naked hook replacing FUN_638e9416 (body render per pocket).
    // This is __thiscall FUN_638e9416(void *this, int pocketIndex, ...).
    // On entry:
    //   ecx   = this (character object)
    //   [esp+4] = pocket index (param_1, 0–17 per GetPocketName)
    // Behavior:
    //   Face (3) and Hair (4): jump to trampoline to execute original function.
    //   All other pockets: return immediately with stack cleanup (naked).
    static __declspec(naked) void hookedPocketRender()
    {
        __asm
        {
            // Read pocket index from stack (param_1, first stack arg after 'this').
            mov     eax, [esp + 4]
            mov     g_lastPocketIndex, eax

            // Face (3) and Hair (4): render normally via trampoline.
            cmp     eax, 6
            je      call_original
            cmp     eax, 7
            je      call_original

            // Every other pocket: return immediately (naked).
            // Log the call before returning.
            pushad
            pushfd
            push    1              ; wentNaked = true
            push    eax            ; pocket index
            call    logPocketCall
            add     esp, 8
            popfd
            popad

            // __thiscall callee cleanup: FUN_638e9416 ends with 'ret 0x14'
            // (20 bytes) at 0x638e9953. Both call sites push 20 bytes of
            // stack data (args + EH cleanup data) before CALL. Using 'ret 0x14'
            // matches the original function's epilogue exactly, preventing
            // ESP corruption. Plain 'ret' leaks 20 bytes/call; 'ret 16'
            // under-cleans by 4 bytes/call — both corrupt ESP over time.
            ret 0x14

        call_original:
            // Log the call before jumping to trampoline.
            pushad
            pushfd
            push    0              ; wentNaked = false (clothed via original)
            push    eax            ; pocket index
            call    logPocketCall
            add     esp, 8
            popfd
            popad

            // Jump to trampoline which executes the original prologue and
            // continues at 0x638E941D (call __EH_prolog3).
            mov     eax, g_trampolineAddr
            jmp     eax
        }
    }

    // Naked hook replacing FUN_638e9b58 (clothing render per pocket).
    // __thiscall: ecx=this, [esp+4]=pocketIndex. Ends with 'ret 0x10'.
    static __declspec(naked) void hookedClothingRender()
    {
        __asm
        {
            // Read pocket index from stack (param_1, first stack arg after 'this').
            mov     eax, [esp + 4]
            mov     g_lastClothingPocketIndex, eax

            // Face (3) and Hair (4): render normally via trampoline.
            cmp     eax, 6
            je      call_original
            cmp     eax, 7
            je      call_original

            // Every other pocket: return immediately (no clothing).
            pushad
            pushfd
            push    1              ; wentNaked = true
            push    eax            ; pocket index
            call    logClothingPocketCall
            add     esp, 8
            popfd
            popad

            ret 0x10

        call_original:
            pushad
            pushfd
            push    0              ; wentNaked = false (clothed via original)
            push    eax            ; pocket index
            call    logClothingPocketCall
            add     esp, 8
            popfd
            popad

            mov     eax, g_clothingTrampolineAddr
            jmp     eax
        }
    }

    SelectiveNakedMode::SelectiveNakedMode()
        : PatchMod{ "Naked Mode",
            "Keeps Face and Hair while making all other equipment slots naked." }
        , m_hookPatch{}
        , m_clothingHookPatch{}
        , m_hooked{ false }
    {
    }

    void SelectiveNakedMode::installHook()
    {
        if (m_hooked) return;

        // Resolve Pleione.dll base at runtime (ASLR-safe).
        HMODULE hPleione = GetModuleHandleA("Pleione.dll");
        if (hPleione == nullptr) {
            log("[SelectiveNakedMode] Pleione.dll not found — hooks NOT installed");
            return;
        }

        uintptr_t pleioneBase = reinterpret_cast<uintptr_t>(hPleione);
        uintptr_t bodyRenderAddr = pleioneBase + 0xE9416;     // FUN_638e9416
        uintptr_t clothingRenderAddr = pleioneBase + 0xE9B58; // FUN_638e9b58

        log("[SelectiveNakedMode] Pleione.dll base: %p", hPleione);
        log("[SelectiveNakedMode] Body render: base + 0xE9416 = %p", (void*)bodyRenderAddr);
        log("[SelectiveNakedMode] Clothing render: base + 0xE9B58 = %p", (void*)clothingRenderAddr);

        // ---- Trampoline for body render (FUN_638e9416) ----
        const size_t trampolineSize = 12;
        void* trampoline = VirtualAlloc(nullptr, trampolineSize,
            MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

        if (!trampoline) {
            log("[SelectiveNakedMode] Failed to allocate body trampoline memory");
            return;
        }

        uint8_t* t = reinterpret_cast<uint8_t*>(trampoline);

        // Copy first 7 bytes of original function (read from memory for ASLR safety).
        // Original: push 5C; mov eax, imm32 (EH handler address)
        memcpy(t, reinterpret_cast<void*>(bodyRenderAddr), 7);

        // Add jmp to bodyRenderAddr + 7 (call __EH_prolog3 at original address).
        intptr_t relJump = (intptr_t)(bodyRenderAddr + 7) - ((intptr_t)trampoline + 12);
        t[7] = 0xE9;                           // jmp rel32
        *(int32_t*)(t + 8) = (int32_t)relJump;

        g_trampolineAddr = reinterpret_cast<uintptr_t>(trampoline);

        // Patch bodyRenderAddr with jump to our hook (5 bytes) + 2 NOPs.
        m_hookPatch.address = bodyRenderAddr;
        m_hookPatch.bytes = {
            0xE9,
            (uint8_t)(((uintptr_t)&hookedPocketRender - (bodyRenderAddr + 5)) & 0xFF),
            (uint8_t)(((uintptr_t)&hookedPocketRender - (bodyRenderAddr + 5)) >> 8) & 0xFF,
            (uint8_t)(((uintptr_t)&hookedPocketRender - (bodyRenderAddr + 5)) >> 16) & 0xFF,
            (uint8_t)(((uintptr_t)&hookedPocketRender - (bodyRenderAddr + 5)) >> 24) & 0xFF,
            0x90, 0x90
        };

        if (!patch(m_hookPatch)) {
            log("[SelectiveNakedMode] patch() FAILED at %p — hook NOT installed", (void*)bodyRenderAddr);
            VirtualFree(trampoline, 0, MEM_RELEASE);
            g_trampolineAddr = 0;
            return;
        }

        // ---- Trampoline for clothing render (FUN_638e9b58) ----
        void* clothingTrampoline = VirtualAlloc(nullptr, trampolineSize,
            MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

        if (!clothingTrampoline) {
            log("[SelectiveNakedMode] Failed to allocate clothing trampoline memory");
            undoPatch(m_hookPatch);
            VirtualFree(trampoline, 0, MEM_RELEASE);
            g_trampolineAddr = 0;
            return;
        }

        uint8_t* ct = reinterpret_cast<uint8_t*>(clothingTrampoline);

        // Copy first 7 bytes of original function (read from memory for ASLR safety).
        // Original: push 58; mov eax, imm32 (EH handler address)
        memcpy(ct, reinterpret_cast<void*>(clothingRenderAddr), 7);

        // Add jmp to clothingRenderAddr + 7.
        intptr_t clothingRelJump = (intptr_t)(clothingRenderAddr + 7) - ((intptr_t)clothingTrampoline + 12);
        ct[7] = 0xE9;
        *(int32_t*)(ct + 8) = (int32_t)clothingRelJump;

        g_clothingTrampolineAddr = reinterpret_cast<uintptr_t>(clothingTrampoline);

        // Patch clothingRenderAddr with jump to our hook.
        m_clothingHookPatch.address = clothingRenderAddr;
        m_clothingHookPatch.bytes = {
            0xE9,
            (uint8_t)(((uintptr_t)&hookedClothingRender - (clothingRenderAddr + 5)) & 0xFF),
            (uint8_t)(((uintptr_t)&hookedClothingRender - (clothingRenderAddr + 5)) >> 8) & 0xFF,
            (uint8_t)(((uintptr_t)&hookedClothingRender - (clothingRenderAddr + 5)) >> 16) & 0xFF,
            (uint8_t)(((uintptr_t)&hookedClothingRender - (clothingRenderAddr + 5)) >> 24) & 0xFF,
            0x90, 0x90
        };

        if (!patch(m_clothingHookPatch)) {
            log("[SelectiveNakedMode] patch() FAILED at %p — clothing hook NOT installed", (void*)clothingRenderAddr);
            undoPatch(m_hookPatch);
            VirtualFree(trampoline, 0, MEM_RELEASE);
            VirtualFree(clothingTrampoline, 0, MEM_RELEASE);
            g_trampolineAddr = 0;
            g_clothingTrampolineAddr = 0;
            return;
        }

        m_hooked = true;
        log("[SelectiveNakedMode] Installed body hook at %p, trampoline at %p",
            (void*)bodyRenderAddr, (void*)g_trampolineAddr);
        log("[SelectiveNakedMode] Installed clothing hook at %p, trampoline at %p",
            (void*)clothingRenderAddr, (void*)g_clothingTrampolineAddr);
        log("[SelectiveNakedMode] Pocket index diagnostics: g_lastPocketIndex (body), g_lastClothingPocketIndex (clothing)");
    }

    void SelectiveNakedMode::removeHook()
    {
        if (!m_hooked) return;

        undoPatch(m_hookPatch);
        undoPatch(m_clothingHookPatch);

        if (g_trampolineAddr) {
            VirtualFree(reinterpret_cast<LPVOID>(g_trampolineAddr), 0, MEM_RELEASE);
            g_trampolineAddr = 0;
        }

        if (g_clothingTrampolineAddr) {
            VirtualFree(reinterpret_cast<LPVOID>(g_clothingTrampolineAddr), 0, MEM_RELEASE);
            g_clothingTrampolineAddr = 0;
        }

        m_hooked = false;
        log("[SelectiveNakedMode] Removed hooks");
    }

    void SelectiveNakedMode::apply()
    {
        log("[SelectiveNakedMode] Toggling %s (m_isEnabled=%d, m_hooked=%d, g_lastPocketIndex=%d, g_lastClothingPocketIndex=%d)",
            m_isEnabled ? "on" : "off", m_isEnabled, m_hooked, g_lastPocketIndex, g_lastClothingPocketIndex);

        if (m_isEnabled) {
            installHook();
        }
        else {
            removeHook();
        }
    }

    void SelectiveNakedMode::onPatchUI()
    {
        if (ImGui::Checkbox(m_patchName.c_str(), &m_isEnabled)) {
            apply();
        }

        if (!m_tooltip.empty() && ImGui::IsItemHovered()) {
            ImGui::SetTooltip(m_tooltip.c_str());
        }

        // Diagnostic: show last pocket index seen by each hook.
        ImGui::Text("Last body pocket index: %d (3=Face, 4=Hair, other=naked)", g_lastPocketIndex);
        ImGui::Text("Last clothing pocket index: %d (3=Face, 4=Hair, other=naked)", g_lastClothingPocketIndex);

        // Diagnostic: show recent body pocket call log.
        if (ImGui::TreeNode("Body pocket call log (recent)")) {
            int start = g_pocketLogCount > 64 ? g_pocketLogCount - 64 : 0;
            for (int i = start; i < g_pocketLogCount; i++) {
                auto& entry = g_pocketLog[i & 63];
                ImGui::Text("Pocket %d: %s", entry.pocketIndex, entry.wentNaked ? "NAKED" : "CLOTHED");
            }
            ImGui::TreePop();
        }

        // Diagnostic: show recent clothing pocket call log.
        if (ImGui::TreeNode("Clothing pocket call log (recent)")) {
            int start = g_clothingLogCount > 64 ? g_clothingLogCount - 64 : 0;
            for (int i = start; i < g_clothingLogCount; i++) {
                auto& entry = g_clothingLog[i & 63];
                ImGui::Text("Pocket %d: %s", entry.pocketIndex, entry.wentNaked ? "NAKED" : "CLOTHED");
            }
            ImGui::TreePop();
        }

        // Diagnostic: show Pleione.dll module base (for ASLR check).
        HMODULE hPleione = GetModuleHandleA("Pleione.dll");
        ImGui::Text("Pleione.dll base: %p", hPleione);
        ImGui::Text("Body hook: base + 0xE9416 = %p",
            hPleione ? (void*)(reinterpret_cast<uintptr_t>(hPleione) + 0xE9416) : nullptr);
        ImGui::Text("Clothing hook: base + 0xE9B58 = %p",
            hPleione ? (void*)(reinterpret_cast<uintptr_t>(hPleione) + 0xE9B58) : nullptr);
    }

    void SelectiveNakedMode::onConfigLoad(const Config& cfg)
    {
        m_isEnabled = cfg.get<bool>(m_configName).value_or(false);

        if (m_isEnabled) {
            apply();
        }
    }

    void SelectiveNakedMode::onConfigSave(Config& cfg)
    {
        cfg.set<bool>(m_configName, m_isEnabled);
    }

}