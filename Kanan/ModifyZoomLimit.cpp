#include <algorithm>

#include <imgui.h>

#include <Scan.hpp>

#include "Log.hpp"
#include "ModifyZoomLimit.hpp"

namespace kanan {
    // The camera constructor reads the maximum zoom distance from this float once we patch it in.
    static float g_zoomLimit{ 15000.0f };

    static constexpr int MIN_LIMIT = 1;
    static constexpr int MAX_LIMIT = 50000;

    ModifyZoomLimit::ModifyZoomLimit()
        : m_isEnabled{ false },
        m_limit{ 15000 },
        m_patch{}
    {
        // pleione::CQuaterViewCamera::CQuaterViewCamera() (Renderer2.dll, ctor + 0x57):
        //   FLD DWORD PTR [maxZoom]    <- we point this at g_zoomLimit (game default is 3000)
        //   PUSH ECX / PUSH ECX
        //   FSTP DWORD PTR [ESP+4]
        //   FLD DWORD PTR [minZoom]
        //   FSTP DWORD PTR [ESP]
        //   CALL pleione::CCamera::SetZoomLimit
        // The constant itself is shared with other code, so only this FLD is redirected.
        auto address = scan("Renderer2.dll", "D9 05 ? ? ? ? 51 51 D9 5C 24 04 D9 05 ? ? ? ? D9 1C 24 E8 ? ? ? ? 8B C6 5E C3");

        if (!address) {
            log("[ModifyZoomLimit] Failed to find the camera zoom limit.");
            return;
        }

        // Make sure the match is inside the quarter-view camera constructor.
        auto renderer = GetModuleHandleA("Renderer2.dll");
        auto ctor = (uintptr_t)GetProcAddress(renderer, "??0CQuaterViewCamera@pleione@@QAE@XZ");

        if (ctor == 0 || *address < ctor || *address > ctor + 0x100) {
            log("[ModifyZoomLimit] Zoom limit code is not where it was expected (%p, ctor %p), not patching.", *address, ctor);
            return;
        }

        log("[ModifyZoomLimit] Found zoom limit at %p", *address);

        auto limitAddress = (uintptr_t)&g_zoomLimit;

        m_patch.address = *address + 2;
        m_patch.bytes = {
            (int16_t)(limitAddress & 0xFF),
            (int16_t)((limitAddress >> 8) & 0xFF),
            (int16_t)((limitAddress >> 16) & 0xFF),
            (int16_t)((limitAddress >> 24) & 0xFF)
        };
    }

    void ModifyZoomLimit::apply() {
        if (m_patch.address == 0) {
            return;
        }

        m_limit = std::clamp(m_limit, MIN_LIMIT, MAX_LIMIT);
        g_zoomLimit = (float)m_limit;

        if (m_isEnabled) {
            log("[ModifyZoomLimit] Setting max zoom distance to %d", m_limit);
            patch(m_patch);
        }
        else {
            log("[ModifyZoomLimit] Restoring the game's max zoom distance");
            undoPatch(m_patch);
        }
    }

    void ModifyZoomLimit::onUI() {
        if (m_patch.address == 0) {
            return;
        }

        if (ImGui::TreeNode("Zoom Limit")) {
            bool changed = ImGui::Checkbox("Change Max Zoom Distance", &m_isEnabled);

            changed |= ImGui::InputInt("Max Zoom Distance", &m_limit, 500, 5000);

            ImGui::TextWrapped("How far the camera can zoom out (1 to 50000). The game's normal limit is 3000.");
            // The value is only read when the game creates a camera (CResourceMgr::NewCamera), not every frame.
            ImGui::TextWrapped("The game only reads this when it sets up the camera. If you don't see a difference, change channels or log in again.");

            if (ImGui::Button("Reset")) {
                m_limit = 15000;
                changed = true;
            }

            if (changed) {
                apply();
            }

            ImGui::TreePop();
        }
    }

    void ModifyZoomLimit::onConfigLoad(const Config& cfg) {
        m_isEnabled = cfg.get<bool>("ModifyZoomLimit.Enabled").value_or(false);
        m_limit = cfg.get<int>("ModifyZoomLimit.Limit").value_or(15000);

        if (m_isEnabled) {
            apply();
        }
    }

    void ModifyZoomLimit::onConfigSave(Config& cfg) {
        cfg.set<bool>("ModifyZoomLimit.Enabled", m_isEnabled);
        cfg.set<int>("ModifyZoomLimit.Limit", m_limit);
    }
}
