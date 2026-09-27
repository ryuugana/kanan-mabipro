#include <algorithm>
#include <cstring>

#include <imgui.h>

#include "Log.hpp"
#include "ModifyRenderDistance.hpp"

using namespace std;

namespace kanan {
    // G13 client (Renderer2.dll), pleione::camera::SetProjectionDesc(near, far, ...) stores the
    // far distance in the camera (+0x40) at +0x0D:
    //   fld dword ptr [ebp+0Ch]
    //   fstp dword ptr [esi+40h]
    // Those 6 bytes jump to our hook, which stores the distance the player chose instead.
    namespace {
        constexpr auto setProjectionDescName = "?SetProjectionDesc@camera@pleione@@QAEXMMM@Z";
        constexpr uintptr_t farStoreOffset = 0x0D;
        constexpr uint8_t farStoreBytes[] = { 0xD9, 0x45, 0x0C, 0xD9, 0x5E, 0x40 };
        constexpr int defaultDistance = 15000;
        constexpr int minDistance = 5000;
        constexpr int maxDistance = 100000;
    }

    static float g_renderDistance{ (float)defaultDistance };
    static uintptr_t g_renderDistanceReturn{ 0 };

    static __declspec(naked) void hookSetFarDistance() {
        __asm {
            fld     dword ptr [ebp + 0x0C]
            fstp    dword ptr [esi + 0x40]
            fld     dword ptr [g_renderDistance]
            fstp    dword ptr [esi + 0x40]
            jmp     dword ptr [g_renderDistanceReturn]
        }
    }

    ModifyRenderDistance::ModifyRenderDistance()
        : m_isEnabled{ false },
        m_distance{ defaultDistance },
        m_isReady{ false },
        m_patch{}
    {
        log("[ModifyRenderDistance] Entering constructor...");

        auto renderer = GetModuleHandleA("Renderer2.dll");
        auto setProjectionDesc = renderer != nullptr ? (uintptr_t)GetProcAddress(renderer, setProjectionDescName) : 0;

        if (setProjectionDesc == 0) {
            log("[ModifyRenderDistance] Failed to find pleione::camera::SetProjectionDesc");
            log("[ModifyRenderDistance] Leaving constructor");
            return;
        }

        auto site = setProjectionDesc + farStoreOffset;

        if (memcmp((const void*)site, farStoreBytes, sizeof(farStoreBytes)) != 0) {
            log("[ModifyRenderDistance] The camera code at %p isn't the one expected", site);
            log("[ModifyRenderDistance] Leaving constructor");
            return;
        }

        auto rel = (int32_t)((uintptr_t)&hookSetFarDistance - (site + 5));

        g_renderDistanceReturn = site + sizeof(farStoreBytes);
        m_patch.address = site;
        m_patch.bytes = {
            0xE9, (int16_t)(rel & 0xFF), (int16_t)((rel >> 8) & 0xFF), (int16_t)((rel >> 16) & 0xFF), (int16_t)((rel >> 24) & 0xFF),
            0x90
        };
        m_isReady = true;

        log("[ModifyRenderDistance] Found the camera's far distance at %p", site);
        log("[ModifyRenderDistance] Leaving constructor");
    }

    void ModifyRenderDistance::apply() {
        if (!m_isReady) {
            return;
        }

        m_distance = clamp(m_distance, minDistance, maxDistance);
        g_renderDistance = (float)m_distance;

        if (m_isEnabled) {
            patch(m_patch);

            log("[ModifyRenderDistance] Render distance set to %d", m_distance);
        }
        else {
            undoPatch(m_patch);

            log("[ModifyRenderDistance] Render distance set back to the game's");
        }
    }

    void ModifyRenderDistance::onUI() {
        if (!m_isReady) {
            return;
        }

        if (ImGui::TreeNode("Render Distance")) {
            ImGui::TextWrapped("Sets how far away the game draws the world.");
            ImGui::Spacing();

            auto changed = ImGui::Checkbox("Enabled##ModifyRenderDistance", &m_isEnabled);

            changed |= ImGui::SliderInt("Distance##ModifyRenderDistance", &m_distance, minDistance, maxDistance);

            if (changed) {
                apply();
            }

            ImGui::TextDisabled("After turning it off, the game's distance may only come back after a restart.");
            ImGui::TreePop();
        }
    }

    void ModifyRenderDistance::onConfigLoad(const Config& cfg) {
        m_isEnabled = cfg.get<bool>("ModifyRenderDistance.Enabled").value_or(false);
        m_distance = clamp(cfg.get<int>("ModifyRenderDistance.Distance").value_or(defaultDistance), minDistance, maxDistance);

        if (m_isEnabled) {
            apply();
        }
    }

    void ModifyRenderDistance::onConfigSave(Config& cfg) {
        cfg.set<bool>("ModifyRenderDistance.Enabled", m_isEnabled);
        cfg.set<int>("ModifyRenderDistance.Distance", m_distance);
    }
}
