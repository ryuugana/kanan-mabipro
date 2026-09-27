#include <Windows.h>

#include <imgui.h>
#include <Config.hpp>

#include "Log.hpp"
#include "Kanan.hpp"
#include "DisplayScaling.hpp"

using namespace std;

namespace kanan {
    // Looked up at runtime, as older versions of Windows don't have them.
    using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE context);
    using SetThreadDpiAwarenessContextFn = HANDLE(WINAPI*)(HANDLE context);
    using GetThreadDpiAwarenessContextFn = HANDLE(WINAPI*)();
    using GetAwarenessFromDpiAwarenessContextFn = int(WINAPI*)(HANDLE context);
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND window);
    using SetProcessDPIAwareFn = BOOL(WINAPI*)();
    using GetWindowDpiAwarenessContextFn = HANDLE(WINAPI*)(HWND window);

    // DPI_AWARENESS_CONTEXT values.
    static const auto perMonitorAwareV2 = (HANDLE)-4;
    static const auto perMonitorAware = (HANDLE)-3;

    // What happened at startup, logged once the log is open.
    static bool g_isRequested{ false };
    static bool g_isApplied{ false };
    static bool g_wasWindowCreated{ false };
    static DWORD g_error{ 0 };
    static const char* g_mode{ "" };

    template <typename F>
    static F user32(const char* name) {
        return (F)GetProcAddress(GetModuleHandleW(L"user32.dll"), name);
    }

    // Whether this process already has a window (which keeps the scaling it was created with).
    static bool hasWindow() {
        auto found = false;

        EnumWindows([](HWND window, LPARAM found) {
            DWORD process{};

            GetWindowThreadProcessId(window, &process);

            if (process == GetCurrentProcessId()) {
                *(bool*)found = true;
                return FALSE;
            }

            return TRUE;
        }, (LPARAM)&found);

        return found;
    }

    void DisplayScaling::applyAtStartup(const string& folder) {
        Config cfg{ folder + "/config.txt" };

        g_isRequested = cfg.get<bool>("DisplayScaling.FullResolution").value_or(false);

        if (!g_isRequested) {
            return;
        }

        g_wasWindowCreated = hasWindow();

        // Per monitor (v2): Windows doesn't scale the game on any monitor.
        if (auto setContext = user32<SetProcessDpiAwarenessContextFn>("SetProcessDpiAwarenessContext")) {
            g_isApplied = setContext(perMonitorAwareV2) || setContext(perMonitorAware);
            g_mode = "per monitor";
        }
        else if (auto setAware = user32<SetProcessDPIAwareFn>("SetProcessDPIAware")) {
            // Windows 7 and 8: aware of the main monitor's scaling.
            g_isApplied = setAware();
            g_mode = "system";
        }

        g_error = g_isApplied ? 0 : GetLastError();
    }

    // Windows' display scaling for the game's window (or the main monitor), as a percentage.
    static UINT displayScale() {
        auto setThread = user32<SetThreadDpiAwarenessContextFn>("SetThreadDpiAwarenessContext");
        auto getDpi = user32<GetDpiForWindowFn>("GetDpiForWindow");
        auto window = g_kanan ? g_kanan->getWindow() : nullptr;

        if (setThread == nullptr || getDpi == nullptr || window == nullptr) {
            auto screen = GetDC(nullptr);
            auto dpi = GetDeviceCaps(screen, LOGPIXELSX);

            ReleaseDC(nullptr, screen);
            return dpi * 100 / 96;
        }

        // Asked as a per monitor aware thread, which Windows tells the real scaling.
        auto previous = setThread(perMonitorAwareV2);
        auto dpi = getDpi(window);

        if (previous != nullptr) {
            setThread(previous);
        }

        return dpi * 100 / 96;
    }

    // Whether the game renders at full resolution (Windows doesn't scale it).
    static bool isFullResolution() {
        auto getContext = user32<GetThreadDpiAwarenessContextFn>("GetThreadDpiAwarenessContext");
        auto getAwareness = user32<GetAwarenessFromDpiAwarenessContextFn>("GetAwarenessFromDpiAwarenessContext");

        if (getContext == nullptr || getAwareness == nullptr) {
            return g_isApplied;
        }

        // DPI_AWARENESS_UNAWARE is 0.
        return getAwareness(getContext()) != 0;
    }

    DisplayScaling::DisplayScaling()
        : m_isEnabled{ false },
        m_isWindowChecked{ false }
    {
        if (!g_isRequested) {
            log("[DisplayScaling] Off; Windows scales the game to fit the screen");
        }
        else if (g_isApplied) {
            log("[DisplayScaling] The game renders at full resolution (%s)%s", g_mode,
                g_wasWindowCreated ? ", but it had already made a window, which Windows may still scale" : "");
        }
        else {
            log("[DisplayScaling] Couldn't make the game render at full resolution (error %lu); a DPI override in "
                "Client.exe's compatibility settings may take precedence", g_error);
        }
    }

    // Checks how Windows actually treats the game's window, once it exists: whether it's stretched,
    // and the size it's drawn at.
    void DisplayScaling::onFrame() {
        auto window = g_kanan ? g_kanan->getWindow() : nullptr;

        if (m_isWindowChecked || window == nullptr) {
            return;
        }

        m_isWindowChecked = true;

        auto getWindowContext = user32<GetWindowDpiAwarenessContextFn>("GetWindowDpiAwarenessContext");
        auto getAwareness = user32<GetAwarenessFromDpiAwarenessContextFn>("GetAwarenessFromDpiAwarenessContext");
        auto scale = displayScale();
        RECT client{};

        GetClientRect(window, &client);

        if (getWindowContext == nullptr || getAwareness == nullptr) {
            log("[DisplayScaling] Game window: %ldx%ld, display scaling %u%%", client.right, client.bottom, scale);
            return;
        }

        // DPI_AWARENESS_UNAWARE is 0: Windows stretches it by the display scaling.
        auto isStretched = getAwareness(getWindowContext(window)) == 0 && scale > 100;

        if (isStretched) {
            log("[DisplayScaling] Game window: drawn at %ldx%ld and stretched by Windows to %ldx%ld (display scaling %u%%)",
                client.right, client.bottom, client.right * scale / 100, client.bottom * scale / 100, scale);
        }
        else {
            log("[DisplayScaling] Game window: drawn at %ldx%ld, its full resolution, not stretched by Windows (display "
                "scaling %u%%)", client.right, client.bottom, scale);
        }
    }

    void DisplayScaling::onUI() {
        if (ImGui::TreeNode("Display Scaling")) {
            auto scale = displayScale();

            ImGui::TextWrapped("Makes the game sharp on screens where Windows display scaling is above 100%%.");
            ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
            ImGui::TextWrapped("Who it helps: players with high resolution screens, like 1440p and 4K monitors and most "
                "laptops, which Windows usually scales to 125%% to 200%% (Settings > System > Display > Scale). At those "
                "settings Windows has the game draw at a lower resolution, then stretches it to fill your screen, which "
                "makes the world, the UI and text look soft or blurry. The higher your scaling, the bigger the "
                "difference. At 100%%, which most 1080p monitors use, Windows doesn't stretch the game, so this changes "
                "nothing.");
            ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
            ImGui::TextWrapped("With it on:");
            ImGui::BulletText("The game draws at your screen's real resolution, so it's sharp.");
            ImGui::BulletText("The UI and text look smaller, as Windows no longer stretches them. Turn on UI Scale and "
                "set it to your display scaling (1.5x for 150%%) to keep them their usual size.");
            ImGui::BulletText("In a window, the game looks smaller at the same resolution; choose a bigger resolution "
                "in the game's options.");
            ImGui::BulletText("Screenshots should save normally, without Screenshot Fix, as the game sees your real "
                "screen size.");
            ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
            ImGui::TextWrapped("It does what setting Client.exe's \"Override high DPI scaling behavior\" to "
                "\"Application\" does (Properties > Compatibility), without changing the game's files. If that "
                "override is set to something else, the override wins; turn it off to use this.");
            ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
            ImGui::Checkbox("Render at full resolution", &m_isEnabled);
            ImGui::TextWrapped("Takes effect the next time you start the game.");
            ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
            ImGui::TextWrapped("Your display scaling: %u%%", scale);

            if (scale <= 100) {
                ImGui::TextWrapped("Windows doesn't scale your display, so this changes nothing for you.");
            }
            else if (isFullResolution()) {
                ImGui::TextWrapped("Now: the game renders at full resolution. UI Scale %.2f keeps the UI its usual size.",
                    scale / 100.0f);
            }
            else {
                ImGui::TextWrapped("Now: Windows stretches the game to fit your screen.");
            }

            ImGui::TreePop();
        }
    }

    void DisplayScaling::onConfigLoad(const Config& cfg) {
        m_isEnabled = cfg.get<bool>("DisplayScaling.FullResolution").value_or(false);
    }

    void DisplayScaling::onConfigSave(Config& cfg) {
        cfg.set<bool>("DisplayScaling.FullResolution", m_isEnabled);
    }
}
