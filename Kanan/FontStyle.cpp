#include <imgui.h>

#include <Scan.hpp>

#include "Log.hpp"
#include "FontStyle.hpp"

using namespace std;

namespace kanan {
    // Finds PATTERN in MODULE and adds a patch that NOPs the two-byte jump at OFFSET into it.
    static bool addJumpRemoval(vector<Patch>& patches, const char* module, const char* pattern, uintptr_t offset) {
        auto address = scan(module, pattern);

        if (!address) {
            log("[FontStyle] Failed to find %s in %s", pattern, module);
            return false;
        }

        Patch p{};

        p.address = *address + offset;
        p.bytes = { 0x90, 0x90 };
        patches.emplace_back(p);

        return true;
    }

    FontStyle::FontStyle()
        : PatchMod{ "Font Style", "" },
        m_trueTypePatches{},
        m_bitmapPatches{},
        m_style{ DEFAULT }
    {
        // TrueType: skip the check that only allows it for some client languages.
        auto isTrueTypeFound = addJumpRemoval(m_trueTypePatches, "Pleione.dll", "83 F8 01 75 43 8B 44 24 04 32 C9", 3);

        // Bitmap: the three places the renderer picks between vector and bitmap fonts.
        auto isBitmapFound = addJumpRemoval(m_bitmapPatches, "Renderer2.dll", "80 BB 80 00 00 00 00 53 FF 75 08 8B CE 74 07", 13) &&
            addJumpRemoval(m_bitmapPatches, "Renderer2.dll", "80 BE 90 00 00 00 00 8B CE 74 11", 9) &&
            addJumpRemoval(m_bitmapPatches, "Renderer2.dll", "80 BE 90 00 00 00 00 0F B7 C0 8B CE 74 0B", 12);

        if (!isTrueTypeFound) {
            m_trueTypePatches.clear();
        }

        if (!isBitmapFound) {
            m_bitmapPatches.clear();
        }
    }

    void FontStyle::onPatchUI() {
        if (m_trueTypePatches.empty() && m_bitmapPatches.empty()) {
            return;
        }

        if (ImGui::Combo("Font Style", &m_style, "Game default\0TrueType fonts\0Bitmap fonts\0\0")) {
            apply();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("TrueType fonts: uses the TrueType text settings whatever the client's language (needed "
                "for Modify Font Size).\nBitmap fonts: draws interface text with bitmap fonts, which can stop lag when "
                "opening windows.\nThe two don't work together, so only one can be chosen.");
        }
    }

    void FontStyle::onConfigLoad(const Config& cfg) {
        m_style = cfg.get<int>("FontStyle.Choice").value_or(DEFAULT);

        if (m_style < DEFAULT || m_style >= STYLE_COUNT) {
            m_style = DEFAULT;
        }

        if (m_style != DEFAULT) {
            apply();
        }
    }

    void FontStyle::onConfigSave(Config& cfg) {
        cfg.set<int>("FontStyle.Choice", m_style);
    }

    void FontStyle::apply() {
        log("[FontStyle] Applying choice %d", m_style);

        for (auto& p : m_trueTypePatches) {
            m_style == TRUETYPE ? patch(p) : undoPatch(p);
        }

        for (auto& p : m_bitmapPatches) {
            m_style == BITMAP ? patch(p) : undoPatch(p);
        }
    }
}
