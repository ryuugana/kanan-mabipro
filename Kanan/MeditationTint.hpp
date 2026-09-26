#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include <imgui.h>

#include "Mod.hpp"

namespace kanan {
    // Tints characters that are meditating with a custom color, using the same per-character
    // color the client applies for character conditions (CharacterCondition "Color" in the data).
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

        bool m_isReady;
        std::mutex m_mutex;
        // Keyed by the character's render entry, which owns the color.
        std::unordered_map<uintptr_t, CharacterColor> m_characters;
        // Hooked render entry vtables and the original color setter of each.
        std::unordered_map<uintptr_t, uintptr_t> m_originalSetColor;

        uint32_t tintRawColor() const;
        bool hookVtable(uintptr_t vtable);
        void setColor(uintptr_t renderEntry, uint32_t rawColor);
        void onCharacterUpdate(uintptr_t character);

        static void __fastcall hookedSetColor(uintptr_t renderEntry, uintptr_t edx, uint32_t rawColor);
    };
}
