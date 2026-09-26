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

        void onFrame() override;

        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

        // Where the labels are drawn.
        enum Mode : int {
            MODE_OVERLAY, // Kanan's overlay, on top of everything
            MODE_IN_GAME, // the client's own name tag layer, covered by the interface like names are
        };

    private:
        struct Label {
            float x;
            float y;
            float life;
            float lifeMax;
            DWORD tick;
        };

        bool m_isEnabled;
        int m_mode;
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
        bool m_canDrawInGame;
        uintptr_t m_nameRangeLoad;

        std::mutex m_labelsMutex;
        std::unordered_map<uintptr_t, Label> m_labels;

        void onCharacterUpdate(uintptr_t character);
        void formatLabel(float life, float lifeMax, char* text, size_t size) const;
        uint32_t labelColor(float life, float lifeMax) const;
    };
}
