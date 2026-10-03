#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <imgui.h>

#include "Mod.hpp"

namespace kanan {
    // Shows the true (unrounded) HP of characters above their in-world name.
    class EntityHP : public Mod {
    public:
        EntityHP();
        virtual ~EntityHP();

        std::string getName() override { return "Entity HP"; }

        void onFrame() override;

        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        struct Label {
            float x;
            float y;
            float life;
            float lifeMax;
            DWORD tick;
        };

        bool m_isEnabled;
        bool m_showPlayers;
        bool m_showMonsters;
        bool m_showMax;
        bool m_colorByHP;
        bool m_textShadow;
        bool m_showBox;
        int m_decimals;
        float m_offsetY;
        float m_fontSize;
        float m_boxPadding;
        float m_boxRounding;
        ImVec4 m_textColor;
        ImVec4 m_boxColor;

        bool m_isHooked;
        uintptr_t m_nameRangeLoad;

        // Screen pixels per pixel of the name tags and of the interface (MabiPro's Bexon.dll sizes
        // them separately).
        float m_tagScale;
        float m_interfaceScale;
        DWORD m_scaleTick;

        // Chat balloons up over characters, in the name tags' pixels; they hide HP labels behind them.
        struct Balloon {
            float x1, y1, x2, y2;
            DWORD tick;
        };

        // Logs window and chat balloon details for diagnosing which elements hide HP labels.
        // Off by default and not in the menu: set EntityHP.DebugLog=true in config.txt.
        bool m_debugLog;

        // DEBUG LOG: raw chat balloon fields per character, to find which mean "showing" and where
        // the balloon is drawn.
        struct BalloonRaw {
            int16_t anchorX, anchorY;   // the name tag's screen position
            uint8_t created, visible;   // CBalloon +0x98, +0x99
            int16_t window[8];          // +0x3C..+0x4A (the CWindow position and pick rects)
            int16_t balloon[8];         // +0xEC..+0xFA (+0xEC/+0xEE are its left/right x)
            DWORD tick;
        };

        std::mutex m_labelsMutex;
        std::unordered_map<uintptr_t, Label> m_labels;
        std::unordered_map<uintptr_t, Balloon> m_balloons;
        std::unordered_map<uintptr_t, BalloonRaw> m_balloonDebug;

        void onCharacterUpdate(uintptr_t character);
        void updateScales();
        void formatLabel(float life, float lifeMax, char* text, size_t size) const;
        uint32_t labelColor(float life, float lifeMax) const;
    };
}
