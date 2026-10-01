#include <algorithm>

#include <imgui.h>

#include <Scan.hpp>

#include "Log.hpp"
#include "ModifyFontSize.hpp"

using namespace std;

namespace kanan {
    // G13 client (Pleione.dll), where the text options are set up for the TrueType font. The code
    // only runs when the client uses its TrueType font settings (its language, or the
    // "Enable TrueType Font" patch):
    //   mov dword ptr [eax+9Ch], 0Bh         (text size 11)
    //   mov dword ptr [eax+0A0h], 0
    // The size is the immediate's low byte.
    namespace {
        constexpr auto sizePattern = "C7 80 9C 00 00 00 ? 00 00 00 C7 80 A0 00 00 00";
        constexpr uintptr_t sizeOffset = 6;
        constexpr int defaultSize = 11;
        constexpr int minSize = 1;
        constexpr int maxSize = 30;
    }

    ModifyFontSize::ModifyFontSize()
        : m_isEnabled{ false },
        m_size{ defaultSize },
        m_originalSize{ defaultSize },
        m_isReady{ false },
        m_patch{}
    {
        log("[ModifyFontSize] Entering constructor...");

        auto address = scan("Pleione.dll", sizePattern);

        if (address) {
            m_patch.address = *address + sizeOffset;
            m_originalSize = *(uint8_t*)m_patch.address;
            m_isReady = true;

            log("[ModifyFontSize] Found the text size at %p (%d)", m_patch.address, m_originalSize);
        }
        else {
            log("[ModifyFontSize] Failed to find the text size");
        }

        log("[ModifyFontSize] Leaving constructor");
    }

    void ModifyFontSize::apply() {
        if (!m_isReady) {
            return;
        }

        if (m_isEnabled) {
            m_size = clamp(m_size, minSize, maxSize);
            m_patch.bytes = { (int16_t)m_size };
            patch(m_patch);

            log("[ModifyFontSize] Text size set to %d", m_size);
        }
        else {
            undoPatch(m_patch);

            log("[ModifyFontSize] Text size set back to %d", m_originalSize);
        }
    }

    void ModifyFontSize::onUI() {
        if (!m_isReady) {
            return;
        }

        if (ImGui::TreeNode("Font Size")) {
            ImGui::TextWrapped("Sets the size of the game's TrueType font text. If you're looking to make the text larger, please look at UI Scaling first. The game's size is %d.", m_originalSize);
            ImGui::Spacing();

            auto changed = ImGui::Checkbox("Enabled##ModifyFontSize", &m_isEnabled);

            changed |= ImGui::SliderInt("Size##ModifyFontSize", &m_size, minSize, maxSize);

            if (changed) {
                apply();
            }

            ImGui::TextDisabled("This setting can cause crashing when the font is set larger than the original. Only works with the TrueType font (Patches > Interface > Enable TrueType Font).\n"
                "Large sizes can cut off text.");
            ImGui::TreePop();
        }
    }

    void ModifyFontSize::onConfigLoad(const Config& cfg) {
        m_isEnabled = cfg.get<bool>("ModifyFontSize.Enabled").value_or(false);
        m_size = clamp(cfg.get<int>("ModifyFontSize.Size").value_or(defaultSize), minSize, maxSize);

        if (m_isEnabled) {
            apply();
        }
    }

    void ModifyFontSize::onConfigSave(Config& cfg) {
        cfg.set<bool>("ModifyFontSize.Enabled", m_isEnabled);
        cfg.set<int>("ModifyFontSize.Size", m_size);
    }
}
