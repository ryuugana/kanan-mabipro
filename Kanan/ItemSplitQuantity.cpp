#include <algorithm>

#include <imgui.h>

#include <Scan.hpp>

#include "Log.hpp"
#include "ItemSplitQuantity.hpp"

using namespace std;

namespace kanan {
    // G13 client (Pleione.dll), where the item split window sets its starting amount (a word at
    // +0x140 of the window, esi):
    //   xor eax, eax
    //   inc eax                              (1)
    //   lea ecx, [ebp-60h]
    //   push ecx
    //   mov word ptr [esi+140h], ax
    // The first 3 bytes become "push quantity / pop eax"; nothing after reads the flags.
    namespace {
        constexpr auto splitPattern = "33 C0 40 8D 4D A0 51 66 89 86 40 01 00 00";
        constexpr int defaultQuantity = 1;
        constexpr int minQuantity = 1;
        constexpr int maxQuantity = 10;
    }

    ItemSplitQuantity::ItemSplitQuantity()
        : m_isEnabled{ false },
        m_quantity{ defaultQuantity },
        m_isReady{ false },
        m_patch{}
    {
        log("[ItemSplitQuantity] Entering constructor...");

        auto address = scan("Pleione.dll", splitPattern);

        if (address) {
            m_patch.address = *address;
            m_isReady = true;

            log("[ItemSplitQuantity] Found the item split window's starting amount at %p", *address);
        }
        else {
            log("[ItemSplitQuantity] Failed to find the item split window's starting amount");
        }

        log("[ItemSplitQuantity] Leaving constructor");
    }

    void ItemSplitQuantity::apply() {
        if (!m_isReady) {
            return;
        }

        if (m_isEnabled) {
            m_quantity = clamp(m_quantity, minQuantity, maxQuantity);
            m_patch.bytes = { 0x6A, (int16_t)m_quantity, 0x58 };
            patch(m_patch);

            log("[ItemSplitQuantity] Item split window starts at %d", m_quantity);
        }
        else {
            undoPatch(m_patch);

            log("[ItemSplitQuantity] Item split window starts at the game's default");
        }
    }

    void ItemSplitQuantity::onUI() {
        if (!m_isReady) {
            return;
        }

        if (ImGui::TreeNode("Item Split Quantity")) {
            ImGui::TextWrapped("Sets the amount the item split window starts at.");
            ImGui::Spacing();

            auto changed = ImGui::Checkbox("Enabled##ItemSplitQuantity", &m_isEnabled);

            changed |= ImGui::SliderInt("Amount##ItemSplitQuantity", &m_quantity, minQuantity, maxQuantity);

            if (changed) {
                apply();
            }

            ImGui::TreePop();
        }
    }

    void ItemSplitQuantity::onConfigLoad(const Config& cfg) {
        m_isEnabled = cfg.get<bool>("ItemSplitQuantity.Enabled").value_or(false);
        m_quantity = clamp(cfg.get<int>("ItemSplitQuantity.Quantity").value_or(defaultQuantity), minQuantity, maxQuantity);

        if (m_isEnabled) {
            apply();
        }
    }

    void ItemSplitQuantity::onConfigSave(Config& cfg) {
        cfg.set<bool>("ItemSplitQuantity.Enabled", m_isEnabled);
        cfg.set<int>("ItemSplitQuantity.Quantity", m_quantity);
    }
}
