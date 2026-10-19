#include <Windows.h>
#include <cstring>

#include <FunctionHook.hpp>
#include <Patch.hpp>
#include <imgui.h>

#include "Log.hpp"
#include "SelectiveNakedMode.hpp"

using namespace std;

namespace kanan {

    static uintptr_t g_trampolineAddr{ 0 };
    static uintptr_t g_clothingTrampolineAddr{ 0 };

    static __declspec(naked) void hookedPocketRender()
    {
        __asm
        {
            mov     eax, [esp + 4]
            cmp     eax, 6
            je      call_original
            cmp     eax, 7
            je      call_original
            ret 0x14

        call_original:
            mov     eax, g_trampolineAddr
            jmp     eax
        }
    }

    static __declspec(naked) void hookedClothingRender()
    {
        __asm
        {
            mov     eax, [esp + 4]
            cmp     eax, 6
            je      call_original
            cmp     eax, 7
            je      call_original
            ret 0x10

        call_original:
            mov     eax, g_clothingTrampolineAddr
            jmp     eax
        }
    }

    SelectiveNakedMode::SelectiveNakedMode()
        : PatchMod{ "Naked Mode",
            "Keeps Face and Hair and removes all clothing." }
        , m_hookPatch{}
        , m_clothingHookPatch{}
        , m_hooked{ false }
    {
    }

    void SelectiveNakedMode::installHook()
    {
        if (m_hooked) return;

        HMODULE hPleione = GetModuleHandleA("Pleione.dll");
        if (hPleione == nullptr) {
            log("[SelectiveNakedMode] Pleione.dll not found");
            return;
        }

        uintptr_t pleioneBase = reinterpret_cast<uintptr_t>(hPleione);
        uintptr_t bodyRenderAddr = pleioneBase + 0xE9416;
        uintptr_t clothingRenderAddr = pleioneBase + 0xE9B58;

        const size_t trampolineSize = 32;

        void* trampoline = VirtualAlloc(nullptr, trampolineSize,
            MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

        if (!trampoline) {
            log("[SelectiveNakedMode] Failed to allocate body trampoline memory");
            return;
        }

        uint8_t* t = reinterpret_cast<uint8_t*>(trampoline);
        memcpy(t, reinterpret_cast<void*>(bodyRenderAddr), 7);

        intptr_t relJump = (intptr_t)(bodyRenderAddr + 7) - ((intptr_t)trampoline + 12);
        t[7] = 0xE9;
        *(int32_t*)(t + 8) = (int32_t)relJump;

        g_trampolineAddr = reinterpret_cast<uintptr_t>(trampoline);

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
        memcpy(ct, reinterpret_cast<void*>(clothingRenderAddr), 7);

        intptr_t clothingRelJump = (intptr_t)(clothingRenderAddr + 7) - ((intptr_t)clothingTrampoline + 12);
        ct[7] = 0xE9;
        *(int32_t*)(ct + 8) = (int32_t)clothingRelJump;

        g_clothingTrampolineAddr = reinterpret_cast<uintptr_t>(clothingTrampoline);

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
    }

    void SelectiveNakedMode::apply()
    {
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