#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include <imgui.h>

#include "Mod.hpp"

namespace kanan {
    // Tints characters that are meditating with a custom color, using the same per-character
    // color the client applies for character conditions (CharacterCondition "Color" in the data).
    // Can also show a Meditation condition icon with their other condition icons (Deadly, ...)
    // while they meditate. Its definition and icon are written to the data folder at startup (the
    // icon made from the client's Meditation skill icon), and it is only shown: the character's
    // actual conditions are left untouched.
    class MeditationTint : public Mod {
    public:
        MeditationTint();
        virtual ~MeditationTint();

        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        struct CharacterColor {
            // Color the client last asked for (from its conditions), restored when meditation ends.
            uint32_t gameColor{ 0 };
            bool isTinted{ false };
            uint32_t tintColor{ 0 };
        };

        bool m_isEnabled;
        ImVec4 m_color;
        float m_strength;
        bool m_showCondition;

        bool m_isReady;
        bool m_isConditionLoaded;
        std::mutex m_mutex;
        // Keyed by the character's render entry, which owns the color.
        std::unordered_map<uintptr_t, CharacterColor> m_characters;
        // Hooked render entry vtables and the original color setter of each.
        std::unordered_map<uintptr_t, uintptr_t> m_originalSetColor;
        // Render entries whose condition icons show the Meditation condition.
        std::unordered_set<uintptr_t> m_conditionShown;

        uint32_t tintRawColor() const;
        bool hookVtable(uintptr_t vtable);
        void setColor(uintptr_t renderEntry, uint32_t rawColor);
        void onCharacterUpdate(uintptr_t character);
        void updateCondition(uintptr_t character, uintptr_t renderEntry, bool isMeditating);

        static void __fastcall hookedSetColor(uintptr_t renderEntry, uintptr_t edx, uint32_t rawColor);
    };
}
