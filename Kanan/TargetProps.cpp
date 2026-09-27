#include <cwchar>

#include <imgui.h>

#include <Scan.hpp>

#include "Log.hpp"
#include "TargetProps.hpp"

namespace kanan {
    static const wchar_t g_enemyOrProp[] = L"enemy|prop";

    TargetProps::TargetProps()
        : PatchMod{ "Target Props", "CTRL-targeting can also pick props while in combat mode." },
        m_enabled{ false },
        m_isAvailable{ false },
        m_patch{}
    {
        log("[TargetProps] Entering constructor...");

        // lea ecx, [ebp-30h] / cmp eax, 1 / jne +7 / push "enemy" / jmp +5 / push ... / nop / call CStringT::operator=
        auto address = scan("Pleione.dll", "8D 4D D0 83 F8 01 75 07 68 ? ? ? ? EB 05 68 ? ? ? ? 90 E8");

        if (!address) {
            log("[TargetProps] Failed to find the combat target filter.");
        }
        else {
            auto operand = *address + 9;
            auto text = *(const wchar_t**)operand;

            if (text == nullptr || wcscmp(text, L"enemy") != 0) {
                log("[TargetProps] Unexpected code at %p, not patching.", operand);
            }
            else {
                auto value = (uintptr_t)g_enemyOrProp;

                m_patch.address = operand;
                m_patch.bytes = { (int16_t)(value & 0xFF), (int16_t)((value >> 8) & 0xFF), (int16_t)((value >> 16) & 0xFF), (int16_t)((value >> 24) & 0xFF) };
                m_isAvailable = true;

                log("[TargetProps] Found the combat target filter at %p", operand);
            }
        }

        log("[TargetProps] Leaving constructor.");
    }

    void TargetProps::apply() {
        if (!m_isAvailable) {
            return;
        }

        log("[TargetProps] Toggling %s", m_enabled ? "on" : "off");

        if (m_enabled) {
            patch(m_patch);
        }
        else {
            undoPatch(m_patch);
        }
    }

    void TargetProps::onPatchUI() {
        if (!m_isAvailable) {
            return;
        }

        if (ImGui::Checkbox("Target Props", &m_enabled)) {
            apply();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("CTRL-targeting can also pick props while in combat mode.");
        }
    }

    void TargetProps::onConfigLoad(const Config& cfg) {
        m_enabled = cfg.get<bool>("TargetProps.Enabled").value_or(false);

        if (m_enabled) {
            apply();
        }
    }

    void TargetProps::onConfigSave(Config& cfg) {
        cfg.set<bool>("TargetProps.Enabled", m_enabled);
    }
}
