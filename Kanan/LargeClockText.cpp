#include <cwchar>

#include <imgui.h>

#include <Scan.hpp>

#include "Log.hpp"
#include "LargeClockText.hpp"

using namespace std;

namespace kanan {
    // G13 client (Pleione.dll), where the clock's text is formatted:
    //   push offset "{1}:{2} {0} "
    //   lea ecx, [ebp+50h]
    //   mov dword ptr [ebp-4], 4
    // The pushed format is swapped for the same format wrapped in the client's <large> tag.
    namespace {
        constexpr auto clockPattern = "68 ? ? ? ? 8D 4D 50 C7 45 FC 04 00 00 00";
        constexpr auto originalFormat = L"{1}:{2} {0} ";
    }

    static const wchar_t g_largeClockFormat[] = L"<large>{1}:{2} {0} </large>";

    // Kept free of C++ objects so the SEH guard can protect it.
    static bool isOriginalFormat(const wchar_t* format) {
        __try {
            return wcscmp(format, originalFormat) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    LargeClockText::LargeClockText()
        : PatchMod{ "Large Clock Text", "" },
        m_isEnabled{ false },
        m_isReady{ false },
        m_patch{}
    {
        log("[LargeClockText] Entering constructor...");

        auto address = scan("Pleione.dll", clockPattern);

        if (!address) {
            log("[LargeClockText] Failed to find the clock format");
            log("[LargeClockText] Leaving constructor");
            return;
        }

        auto format = *(const wchar_t**)(*address + 1);

        if (!isOriginalFormat(format)) {
            log("[LargeClockText] The clock format at %p isn't the one expected", *address);
            log("[LargeClockText] Leaving constructor");
            return;
        }

        auto newFormat = (uintptr_t)&g_largeClockFormat[0];

        m_patch.address = *address + 1;
        m_patch.bytes = {
            (int16_t)(newFormat & 0xFF), (int16_t)((newFormat >> 8) & 0xFF),
            (int16_t)((newFormat >> 16) & 0xFF), (int16_t)((newFormat >> 24) & 0xFF)
        };
        m_isReady = true;

        log("[LargeClockText] Found the clock format at %p", *address);
        log("[LargeClockText] Leaving constructor");
    }

    void LargeClockText::apply() {
        if (!m_isReady) {
            return;
        }

        log("[LargeClockText] Toggling %s", m_isEnabled ? "on" : "off");

        if (m_isEnabled) {
            patch(m_patch);
        }
        else {
            undoPatch(m_patch);
        }
    }

    void LargeClockText::onPatchUI() {
        if (!m_isReady) {
            return;
        }

        if (ImGui::Checkbox("Large Clock Text", &m_isEnabled)) {
            apply();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows the in-game clock in large text.\nThe text can get cut off.");
        }
    }

    void LargeClockText::onConfigLoad(const Config& cfg) {
        m_isEnabled = cfg.get<bool>("LargeClockText.Enabled").value_or(false);

        if (m_isEnabled) {
            apply();
        }
    }

    void LargeClockText::onConfigSave(Config& cfg) {
        cfg.set<bool>("LargeClockText.Enabled", m_isEnabled);
    }
}
