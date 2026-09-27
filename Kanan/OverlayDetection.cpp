#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <thread>

#include <Windows.h>
#include <Shlwapi.h>

#include <imgui.h>
#include <String.hpp>

#include "Log.hpp"
#include "Kanan.hpp"
#include "OverlayDetection.hpp"

using namespace std;

namespace kanan {
    namespace {
        struct KnownOverlay {
            const wchar_t* module;
            const char* name;
            // Known to stop Kanan's menu from showing.
            bool hidesMenu;
        };

        // The 32-bit modules overlays load into the games they draw in.
        constexpr KnownOverlay knownOverlays[] = {
            { L"RTSSHooks.dll", "RivaTuner Statistics Server (MSI Afterburner's overlay)", true },
            { L"GameOverlayRenderer.dll", "Steam overlay", false },
            { L"DiscordHook.dll", "Discord overlay", false },
            { L"graphics-hook32.dll", "OBS game capture", false },
            { L"nvspcap.dll", "NVIDIA overlay/ShadowPlay", false },
            { L"fraps32.dll", "Fraps", false },
        };
    }

    // How often to look for overlays, which can attach to the game at any time.
    constexpr unsigned long long checkInterval = 5000;

    // D3D9 device vtable entries.
    constexpr size_t resetIndex = 16;
    constexpr size_t presentIndex = 17;

    static string format(const char* text, ...) {
        va_list args{};

        va_start(args, text);
        auto result = formatString(text, args);
        va_end(args);

        return result;
    }

    static const KnownOverlay* findKnownOverlay(const wstring& module) {
        for (auto& overlay : knownOverlays) {
            if (_wcsicmp(overlay.module, module.c_str()) == 0) {
                return &overlay;
            }
        }

        return nullptr;
    }

    // Whether SIZE bytes at ADDRESS can be read (as code, when CODE is set).
    static bool isReadable(uintptr_t address, size_t size, bool code) {
        MEMORY_BASIC_INFORMATION info{};

        if (address == 0 || VirtualQuery((LPCVOID)address, &info, sizeof(info)) == 0 || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        {
            return false;
        }

        constexpr DWORD executable = PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        constexpr DWORD readable = executable | PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY;

        return (info.Protect & (code ? executable : readable)) != 0 &&
            address + size <= (uintptr_t)info.BaseAddress + info.RegionSize;
    }

    // Where the jump at ADDRESS goes, if there is one there: the ways hooks jump to their code.
    static uintptr_t jumpTarget(uintptr_t address) {
        if (!isReadable(address, 7, true)) {
            return 0;
        }

        auto code = (const uint8_t*)address;

        switch (code[0]) {
        case 0xE9: // jmp rel32
            return address + 5 + *(const int32_t*)(code + 1);

        case 0xEB: // jmp rel8
            return address + 2 + (int8_t)code[1];

        case 0x68: // push imm32; ret
            return code[5] == 0xC3 ? *(const uint32_t*)(code + 1) : 0;

        case 0xB8: // mov eax, imm32; jmp eax
            return code[5] == 0xFF && code[6] == 0xE0 ? *(const uint32_t*)(code + 1) : 0;

        case 0xFF: { // jmp [imm32]
            auto pointer = *(const uint32_t*)(code + 2);

            return code[1] == 0x25 && isReadable(pointer, 4, false) ? *(const uint32_t*)pointer : 0;
        }

        default:
            return 0;
        }
    }

    // The module containing ADDRESS, and its file name.
    static HMODULE moduleOf(uintptr_t address, wstring* name = nullptr) {
        HMODULE module{ nullptr };

        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)address, &module))
        {
            return nullptr;
        }

        if (name != nullptr) {
            wchar_t path[MAX_PATH]{};

            GetModuleFileNameW(module, path, MAX_PATH);
            *name = PathFindFileNameW(path);
        }

        return module;
    }

    // The module of another hook on a function Kanan hooked (HOOK), if there is one. A hook that
    // came after Kanan's jumps to its own code from the start of the function; one that came before
    // does from the start of Kanan's trampoline (the function's original first instructions).
    static wstring otherHook(const FunctionHook& hook) {
        auto own = moduleOf((uintptr_t)&otherHook);
        auto home = moduleOf(hook.getTarget());
        auto address = hook.getTarget();
        auto isTrampolineFollowed = false;

        for (int i = 0; i < 8; ++i) {
            auto target = jumpTarget(address);

            if (target == 0) {
                break;
            }

            wstring name{};
            auto module = moduleOf(target, &name);

            if (module == own) {
                // Kanan's hook: continue from where it calls the original function.
                if (isTrampolineFollowed) {
                    break;
                }

                isTrampolineFollowed = true;
                address = hook.getOriginal();
            }
            else if (module == home || module == nullptr) {
                // A jump within the function's module (hot patching), or to a stub outside any
                // module: see where it goes.
                address = target;
            }
            else {
                return name;
            }
        }

        // A stub that goes on in a way we don't follow.
        if (address != hook.getTarget() && address != hook.getOriginal() && moduleOf(address) == nullptr) {
            return L"code outside any module";
        }

        return {};
    }

    OverlayDetection::OverlayDetection()
        : m_warnConflicts{ true },
        m_isWarned{ false },
        m_isChecked{ false },
        m_nextCheck{ 0 },
        m_findings{},
        m_overlays{}
    {
    }

    void OverlayDetection::onFrame() {
        auto now = GetTickCount64();

        if (now < m_nextCheck) {
            return;
        }

        m_nextCheck = now + checkInterval;
        check();
    }

    void OverlayDetection::check() {
        auto d3d9 = g_kanan->getD3D9Hook();

        if (d3d9 == nullptr || !d3d9->isValid()) {
            return;
        }

        // Overlays that hook the game's drawing functions, known or not.
        struct Function {
            const char* name;
            const FunctionHook& hook;
            size_t vtableIndex;
        };

        const Function functions[] = {
            { "Present", d3d9->getPresentHook(), presentIndex },
            { "Reset", d3d9->getResetHook(), resetIndex },
        };

        for (auto& function : functions) {
            auto module = otherHook(function.hook);

            if (!module.empty()) {
                auto known = findKnownOverlay(module);

                addFinding(format("%s is also hooked by %s", function.name, narrow(module).c_str()),
                    known ? known->name : narrow(module), known && known->hidesMenu);
            }

            // Overlays can also hook the game's device instead of the function.
            auto device = d3d9->getDevice();

            if (device != nullptr) {
                auto entry = (*(uintptr_t**)device)[function.vtableIndex];

                if (entry != function.hook.getTarget()) {
                    wstring name{};

                    if (moduleOf(entry, &name) == nullptr) {
                        name = L"code outside any module";
                    }

                    auto known = findKnownOverlay(name);

                    addFinding(format("The game's %s is redirected to %s", function.name, narrow(name).c_str()),
                        known ? known->name : narrow(name), known && known->hidesMenu);
                }
            }
        }

        // Known overlays, however they draw.
        for (auto& overlay : knownOverlays) {
            if (GetModuleHandleW(overlay.module) != nullptr) {
                addFinding(format("%s is loaded (%s)", narrow(overlay.module).c_str(), overlay.name), overlay.name,
                    overlay.hidesMenu);
            }
        }

        if (!m_isChecked) {
            m_isChecked = true;

            if (m_findings.empty()) {
                log("[OverlayDetection] No overlays found yet; checking again every %llu seconds", checkInterval / 1000);
            }
        }
    }

    void OverlayDetection::addFinding(const string& finding, const string& overlay, bool hidesMenu) {
        if (!m_findings.insert(finding).second) {
            return;
        }

        log("[OverlayDetection] %s%s", finding.c_str(), hidesMenu ? " (known to stop Kanan's menu from showing)" : "");

        if (find(m_overlays.begin(), m_overlays.end(), overlay) == m_overlays.end()) {
            m_overlays.push_back(overlay);
        }

        if (hidesMenu) {
            warn(overlay);
        }
    }

    void OverlayDetection::warn(const string& overlay) {
        if (!m_warnConflicts || m_isWarned) {
            return;
        }

        m_isWarned = true;

        auto text = widen(format(
            "%s is running in Mabinogi, and it can stop Kanan's menu from showing.\n\n"
            "If you can't see Kanan's menu, close MSI Afterburner and RivaTuner Statistics Server (RTSS). Or, to keep "
            "them, turn RTSS off for Mabinogi only:\n\n"
            "1. Open RivaTuner Statistics Server.\n"
            "2. Click Add, and choose Client.exe in your MabiPro folder.\n"
            "3. With Client.exe selected, set Application detection level to None.\n\n"
            "If Kanan's menu shows fine, you can turn this warning off in Kanan: Configurable > Overlay Detection.",
            overlay.c_str()));

        // Shown on its own thread so the game keeps running, and without the game's window as its
        // owner so the game still takes input.
        thread{ [text] {
            MessageBoxW(nullptr, text.c_str(), L"Kanan", MB_OK | MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND);
        } }.detach();
    }

    void OverlayDetection::onUI() {
        if (ImGui::TreeNode("Overlay Detection")) {
            ImGui::TextWrapped("Looks for overlays that draw in the game like Kanan does, and writes them to Kanan's "
                "log. Some, like MSI Afterburner's RivaTuner Statistics Server (RTSS), can stop Kanan's menu from "
                "showing; Kanan warns with a popup when one of those is found.");
            ImGui::Dummy(ImVec2{ 10.0f, 10.0f });

            if (m_overlays.empty()) {
                ImGui::TextWrapped("No overlays found.");
            }
            else {
                ImGui::TextWrapped("Found:");

                for (auto& overlay : m_overlays) {
                    ImGui::BulletText("%s", overlay.c_str());
                }
            }

            ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
            ImGui::Checkbox("Warn about overlays that can hide Kanan's menu", &m_warnConflicts);
            ImGui::TreePop();
        }
    }

    void OverlayDetection::onConfigLoad(const Config& cfg) {
        m_warnConflicts = cfg.get<bool>("OverlayDetection.WarnConflicts").value_or(true);
    }

    void OverlayDetection::onConfigSave(Config& cfg) {
        cfg.set<bool>("OverlayDetection.WarnConflicts", m_warnConflicts);
    }
}
