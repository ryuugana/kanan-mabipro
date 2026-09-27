#include <algorithm>

#include <imgui.h>

#include <Scan.hpp>

#include "Log.hpp"
#include "ScreenshotQuality.hpp"

namespace kanan {
    static constexpr int GAME_QUALITY = 90;

    ScreenshotQuality::ScreenshotQuality()
        : m_isEnabled{ false },
        m_quality{ GAME_QUALITY },
        m_patches{}
    {
        // Pleione.dll passes a hard-coded JPEG quality of 90 (PUSH 5Ah) down to
        // pleione::CRendererContext::CaptureScreen, which hands it to esl::CIJL::WriteJPEG (EXL.dll).
        // We change that PUSH imm8 instead of hooking the JPEG writer.
        // The 50% quality used for the "report a player" screenshot upload is left alone.
        struct Site {
            const char* pattern;
            int offset;
        };

        const Site sites[] = {
            // Capture of part of the screen: PUSH 5Ah / PUSH [EDX+4] / PUSH [EBP+8] / CALL [EAX+148h]
            { "6A 5A FF 72 04 FF 75 08 FF 90 48 01 00 00", 1 },
            // The three callers that pass quality 90 to the full-screen capture helper (Pleione+0x645D):
            // two in a network message handler, one elsewhere.
            { "8B 4E 2C 6A 5A 50 8D 45 EC 50 C6 45 FC 02 E8", 4 },
            { "8B 4E 2C 6A 5A 50 8D 45 E4 50 C6 45 FC 03 E8", 4 },
            { "8B 4D F0 6A 5A 50 8D 45 EC 50 C7 45 FC 07 00 00 00 E8", 4 },
        };

        for (const auto& site : sites) {
            auto address = scan("Pleione.dll", site.pattern);

            if (!address) {
                log("[ScreenshotQuality] Failed to find pattern %s", site.pattern);
                m_patches.clear();
                return;
            }

            auto qualityAddress = *address + site.offset;

            if (*(uint8_t*)qualityAddress != GAME_QUALITY) {
                log("[ScreenshotQuality] Unexpected quality byte at %p, not patching.", qualityAddress);
                m_patches.clear();
                return;
            }

            log("[ScreenshotQuality] Found screenshot quality at %p", qualityAddress);

            Patch p{};

            p.address = qualityAddress;
            p.bytes = { GAME_QUALITY };

            m_patches.emplace_back(p);
        }
    }

    void ScreenshotQuality::apply() {
        if (m_patches.empty()) {
            return;
        }

        m_quality = std::clamp(m_quality, 1, 100);

        if (m_isEnabled) {
            log("[ScreenshotQuality] Setting screenshot quality to %d", m_quality);

            for (auto& p : m_patches) {
                // PUSH imm8 is sign-extended, 1..100 always fits.
                p.bytes = { (int16_t)m_quality };
                patch(p);
            }
        }
        else {
            log("[ScreenshotQuality] Restoring the game's screenshot quality");

            for (auto& p : m_patches) {
                undoPatch(p);
            }
        }
    }

    void ScreenshotQuality::onUI() {
        if (m_patches.empty()) {
            return;
        }

        if (ImGui::TreeNode("Screenshot Quality")) {
            bool changed = ImGui::Checkbox("Change Screenshot Quality", &m_isEnabled);

            changed |= ImGui::SliderInt("Quality", &m_quality, 1, 100);

            ImGui::TextWrapped("JPEG quality of screenshots, from 1 (smallest files) to 100 (best looking). The game normally uses 90.");

            if (changed) {
                apply();
            }

            ImGui::TreePop();
        }
    }

    void ScreenshotQuality::onConfigLoad(const Config& cfg) {
        m_isEnabled = cfg.get<bool>("ScreenshotQuality.Enabled").value_or(false);
        m_quality = cfg.get<int>("ScreenshotQuality.Quality").value_or(GAME_QUALITY);

        if (m_isEnabled) {
            apply();
        }
    }

    void ScreenshotQuality::onConfigSave(Config& cfg) {
        cfg.set<bool>("ScreenshotQuality.Enabled", m_isEnabled);
        cfg.set<int>("ScreenshotQuality.Quality", m_quality);
    }
}
