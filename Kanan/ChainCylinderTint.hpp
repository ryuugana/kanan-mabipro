#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include <Windows.h>
#include <imgui.h>

#include <Config.hpp>

#include "Mod.hpp"

namespace kanan {
    // Tints characters that have Chain Cylinder (SkillID 35015, 0x88C7) enabled,
    // using the same per-character color hook as MeditationTint (render entry
    // SetColor). The active check is the one from history: the client's
    // skill-ID 35015 lookup (push 0x88C7 -> vtable +0x60 getter -> skill-enabled
    // predicate), re-expressed here through the character's core::ISkillMgr so
    // no hardcoded client addresses are needed across reloads.
    class ChainCylinderTint : public Mod {
    public:
        ChainCylinderTint();
        virtual ~ChainCylinderTint();

        std::string getName() override { return "Chain Cylinder Tint"; }

        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        struct CharacterColor {
            // Color the client last asked for, restored when the tint lifts.
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
        // How many of them are tinted.
        size_t m_tintedCount{ 0 };
        // Hooked render entry vtables and the original color setter of each.
        std::unordered_map<uintptr_t, uintptr_t> m_originalSetColor;

        uint32_t tintRawColor() const;
        bool hookVtable(uintptr_t vtable);
        void setColor(uintptr_t renderEntry, uint32_t rawColor);
        void onCharacterUpdate(uintptr_t character);

        static void __fastcall hookedSetColor(uintptr_t renderEntry, uintptr_t edx, uint32_t rawColor);
    };
}
