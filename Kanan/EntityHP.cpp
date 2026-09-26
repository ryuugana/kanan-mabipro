#include <cfloat>
#include <cstdio>

#include <imgui.h>

#include <Scan.hpp>

#include "Log.hpp"
#include "CharacterHook.hpp"
#include "EntityHP.hpp"
#include "UIScale.hpp"

using namespace std;

namespace kanan {
    // G13 client layout (Pleione.dll / Standard.dll):
    //   pleione::CCharacter +0x198 -> render entry, render entry +0x0C -> CCharacterSticker (the in-world name)
    //   CCharacterSticker +0x10 layer its texts are drawn at, +0x14 depth, +0x54 camera distance,
    //   +0x58/+0x5A screen x/y, +0xA5 enabled
    //   CCharacter vtable +0x50 -> core::IParameter
    namespace offsets {
        constexpr uintptr_t renderEntry = 0x198;
        constexpr uintptr_t sticker = 0x0C;
        constexpr uintptr_t stickerLayer = 0x10;
        constexpr uintptr_t stickerDepth = 0x14;
        constexpr uintptr_t stickerDistance = 0x54;
        constexpr uintptr_t stickerScreenX = 0x58;
        constexpr uintptr_t stickerScreenY = 0x5A;
        constexpr uintptr_t stickerEnabled = 0xA5;
        constexpr uintptr_t getParameter = 0x50;
    }

    using GetFloat = float(__thiscall*)(uintptr_t object);
    using GetBool = bool(__thiscall*)(uintptr_t object);
    using GetPointer = uintptr_t(__thiscall*)(uintptr_t object);

    static GetFloat g_getLife{ nullptr };
    static GetFloat g_getLifeMax{ nullptr };
    static GetBool g_isNPC{ nullptr };

    // Text drawing of the client's interface (pleione::CWindowMgr), the same calls its name tags
    // use: the text is drawn into the interface's 2D scene at the tag's layer, so windows cover
    // it the way they cover names.
    using StringConstruct = void*(__thiscall*)(void* string, const wchar_t* text);
    using StringDestruct = void(__thiscall*)(void* string);
    using MeasureString = void(__thiscall*)(uintptr_t windowMgr, const void* string, uint32_t* width, uint32_t* height, uint32_t flags);
    using RenderString = void(__thiscall*)(uintptr_t windowMgr, const void* string, int32_t x, int32_t y, float layer,
        uint32_t color, uint32_t outlineColor, uint32_t outline, uint32_t unknown1, uint32_t unknown2, uintptr_t scene);

    static uintptr_t* g_windowMgr{ nullptr };
    static StringConstruct g_stringConstruct{ nullptr };
    static StringDestruct g_stringDestruct{ nullptr };
    static MeasureString g_measureString{ nullptr };
    static RenderString g_renderString{ nullptr };

    struct NameTag {
        float x;
        float y;
        float layer;
        float life;
        float lifeMax;
        bool isNPC;
    };

    // Colors are 0xAARRGGBB for the client and 0xAABBGGRR for ImGui.
    static uint32_t toARGB(ImU32 color) {
        return (color & 0xFF00FF00) | ((color & 0xFF) << 16) | ((color >> 16) & 0xFF);
    }

    // Draws a line of text centered above the name tag with the client's own text rendering.
    // Kept free of C++ objects so the SEH guard can protect it.
    static void drawInGame(const NameTag& tag, const wchar_t* text, float offsetY, uint32_t color, bool outline) {
        __try {
            auto windowMgr = *g_windowMgr;

            if (windowMgr == 0) {
                return;
            }

            alignas(8) uint8_t string[16]{};
            uint32_t width{}, height{};

            g_stringConstruct(string, text);
            g_measureString(windowMgr, string, &width, &height, 0);

            auto x = (int32_t)tag.x - (int32_t)width / 2;
            auto y = (int32_t)(tag.y - offsetY) - (int32_t)height;
            auto outlineColor = outline ? (color & 0xFF000000) : 0;

            g_renderString(windowMgr, string, x, y, tag.layer, color, outlineColor, outline ? 1 : 0, 0, 0, 0);
            g_stringDestruct(string);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    // Reads the name tag of a character the same way the client decides to draw it.
    // Kept free of C++ objects so the SEH guard can protect it.
    static bool readNameTag(uintptr_t character, uintptr_t nameRangeLoad, NameTag& out) {
        __try {
            auto renderEntry = *(uintptr_t*)(character + offsets::renderEntry);

            if (renderEntry == 0) {
                return false;
            }

            auto sticker = *(uintptr_t*)(renderEntry + offsets::sticker);

            if (sticker == 0 || *(uint8_t*)(sticker + offsets::stickerEnabled) == 0) {
                return false;
            }

            // Behind the camera or past the far plane.
            auto depth = *(float*)(sticker + offsets::stickerDepth);

            if (depth < 0.0f || depth > 1.0f) {
                return false;
            }

            // Beyond name range. Read through the client's FLD operand so a patch
            // that raises the range (Display Names From Far) is honored.
            if (nameRangeLoad != 0) {
                auto range = **(float**)(nameRangeLoad + 2);

                if (*(float*)(sticker + offsets::stickerDistance) >= range) {
                    return false;
                }
            }

            auto getParameter = (GetPointer)(*(uintptr_t**)character)[offsets::getParameter / sizeof(uintptr_t)];
            auto parameter = getParameter(character);

            if (parameter == 0) {
                return false;
            }

            out.x = *(int16_t*)(sticker + offsets::stickerScreenX);
            out.y = *(int16_t*)(sticker + offsets::stickerScreenY);
            out.layer = *(float*)(sticker + offsets::stickerLayer);
            out.life = g_getLife(parameter);
            out.lifeMax = g_getLifeMax(parameter);
            out.isNPC = g_isNPC(character);

            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    EntityHP::EntityHP()
        : m_isEnabled{ false },
        m_mode{ MODE_OVERLAY },
        m_showPlayers{ true },
        m_showMonsters{ true },
        m_showMax{ true },
        m_colorByHP{ true },
        m_textShadow{ true },
        m_showBox{ false },
        m_decimals{ 1 },
        m_offsetY{ 30.0f },
        m_fontSize{ 16.0f },
        m_boxPadding{ 3.0f },
        m_boxRounding{ 3.0f },
        m_textColor{ 1.0f, 1.0f, 1.0f, 1.0f },
        m_boxColor{ 0.0f, 0.0f, 0.0f, 0.6f },
        m_isHooked{ false },
        m_canDrawInGame{ false },
        m_nameRangeLoad{ 0 },
        m_labelsMutex{},
        m_labels{}
    {
        log("[EntityHP] Entering constructor...");

        auto standard = GetModuleHandleA("Standard.dll");

        if (standard != nullptr) {
            g_getLife = (GetFloat)GetProcAddress(standard, "?GetLife@IParameterBase2@core@@QAEMXZ");
            g_getLifeMax = (GetFloat)GetProcAddress(standard, "?GetLifeMax@IParameterBase2@core@@QAEMXZ");
            g_isNPC = (GetBool)GetProcAddress(standard, "?IsNPC@ICharacter@core@@QBE_NXZ");
        }

        if (g_getLife == nullptr || g_getLifeMax == nullptr || g_isNPC == nullptr) {
            log("[EntityHP] Failed to find Standard.dll exports");
            log("[EntityHP] Leaving constructor");
            return;
        }

        // FLD [name range] inside pleione::CCharacterSticker's per-frame update.
        auto rangeLoad = scan("Pleione.dll", "D9 05 ? ? ? ? D8 5B 54 66 89 43 60");

        if (rangeLoad) {
            m_nameRangeLoad = *rangeLoad;
        }
        else {
            log("[EntityHP] Failed to find name range, labels will not be range limited");
        }

        // pleione::CWindowMgr's text measuring and drawing, used by the name tags themselves.
        auto esl = GetModuleHandleA("ESL.dll");
        auto pleione = GetModuleHandleA("Pleione.dll");
        auto measureString = scan("Pleione.dll", "68 44 01 00 00 B8 ? ? ? ? E8 ? ? ? ? 8B 5D 08 8B 75 0C 8B 7D 10 89 8D B4 FE FF FF");
        auto renderString = scan("Pleione.dll", "68 58 01 00 00 B8 ? ? ? ? E8 ? ? ? ? 8B 45 08 8B 5D 2C 8B F9 8D 8D 9C FE FF FF");

        if (esl != nullptr && pleione != nullptr && measureString && renderString) {
            g_windowMgr = (uintptr_t*)GetProcAddress(pleione, "?s_pInstanceBlock@?$TSingleton@VCWindowMgr@pleione@@@esl@@0PAEA");
            g_stringConstruct = (StringConstruct)GetProcAddress(esl,
                "??0?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QAE@PB_W@Z");
            g_stringDestruct = (StringDestruct)GetProcAddress(esl,
                "??1?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QAE@XZ");
            g_measureString = (MeasureString)*measureString;
            g_renderString = (RenderString)*renderString;
        }

        m_canDrawInGame = g_windowMgr != nullptr && g_stringConstruct != nullptr && g_stringDestruct != nullptr &&
            g_measureString != nullptr && g_renderString != nullptr;

        if (!m_canDrawInGame) {
            log("[EntityHP] Failed to find the client's text drawing, only the overlay is available");
        }

        m_isHooked = addCharacterUpdateCallback([this](uintptr_t character) {
            if (m_isEnabled) {
                onCharacterUpdate(character);
            }
        });

        log("[EntityHP] Leaving constructor");
    }

    EntityHP::~EntityHP() {
    }

    void EntityHP::formatLabel(float life, float lifeMax, char* text, size_t size) const {
        if (m_showMax) {
            snprintf(text, size, "%.*f / %.*f", m_decimals, life, m_decimals, lifeMax);
        }
        else {
            snprintf(text, size, "%.*f", m_decimals, life);
        }
    }

    ImU32 EntityHP::labelColor(float life, float lifeMax) const {
        if (m_colorByHP && lifeMax > 0.0f) {
            auto fraction = life / lifeMax;

            fraction = fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);

            return IM_COL32((int)(255 * (1.0f - fraction)), (int)(255 * fraction), 64, (int)(255 * m_textColor.w));
        }

        return ImGui::ColorConvertFloat4ToU32(m_textColor);
    }

    void EntityHP::onFrame() {
        if (!m_isEnabled || (m_mode == MODE_IN_GAME && m_canDrawInGame)) {
            scoped_lock<mutex> _{ m_labelsMutex };

            m_labels.clear();
            return;
        }

        auto& io = ImGui::GetIO();
        auto drawList = ImGui::GetBackgroundDrawList();
        auto font = ImGui::GetFont();
        auto now = GetTickCount();
        // Name tag positions are in the interface's (possibly scaled) virtual pixels.
        auto uiScale = UIScale::current();
        char text[64];

        scoped_lock<mutex> _{ m_labelsMutex };

        for (auto it = m_labels.begin(); it != m_labels.end();) {
            auto& label = it->second;

            // Characters that stopped updating (despawned, out of range) drop off.
            if (now - label.tick > 250) {
                it = m_labels.erase(it);
                continue;
            }

            ++it;

            auto x = label.x * uiScale;
            auto y = label.y * uiScale;

            if (x < -50.0f || y < -50.0f || x > io.DisplaySize.x + 50.0f || y > io.DisplaySize.y + 50.0f) {
                continue;
            }

            formatLabel(label.life, label.lifeMax, text, sizeof(text));

            auto color = labelColor(label.life, label.lifeMax);
            auto size = font->CalcTextSizeA(m_fontSize, FLT_MAX, 0.0f, text);
            ImVec2 pos{ x - size.x / 2.0f, y - m_offsetY - size.y };

            if (m_showBox) {
                drawList->AddRectFilled(
                    ImVec2{ pos.x - m_boxPadding, pos.y - m_boxPadding },
                    ImVec2{ pos.x + size.x + m_boxPadding, pos.y + size.y + m_boxPadding },
                    ImGui::ColorConvertFloat4ToU32(m_boxColor), m_boxRounding);
            }

            if (m_textShadow) {
                drawList->AddText(font, m_fontSize, ImVec2{ pos.x + 1.0f, pos.y + 1.0f }, IM_COL32(0, 0, 0, 200), text);
            }

            drawList->AddText(font, m_fontSize, pos, color, text);
        }
    }

    void EntityHP::onUI() {
        if (!m_isHooked) {
            return;
        }

        if (ImGui::TreeNode(getName().c_str())) {
            ImGui::TextWrapped("Shows the true HP of characters above their name.");
            ImGui::Spacing();
            ImGui::Checkbox("Enabled##EntityHP", &m_isEnabled);

            if (m_canDrawInGame) {
                ImGui::Combo("Display##EntityHP", &m_mode, "Overlay\0In-game\0");
                ImGui::TextDisabled(m_mode == MODE_IN_GAME ?
                    "Drawn by the game with its own font, layered like names:\nwindows cover it and it hides with names." :
                    "Drawn by Kanan on top of everything, with its own font, size and box.");
            }

            auto isOverlay = m_mode == MODE_OVERLAY || !m_canDrawInGame;

            ImGui::Checkbox("Players##EntityHP", &m_showPlayers);
            ImGui::Checkbox("NPCs and monsters##EntityHP", &m_showMonsters);
            ImGui::Checkbox("Show max HP##EntityHP", &m_showMax);
            ImGui::SliderInt("Decimals##EntityHP", &m_decimals, 0, 2);
            ImGui::SliderFloat("Height above name##EntityHP", &m_offsetY, 0.0f, 150.0f, "%.0f px");

            if (isOverlay) {
                ImGui::SliderFloat("Text size##EntityHP", &m_fontSize, 10.0f, 32.0f, "%.0f");
            }

            ImGui::Spacing();
            ImGui::Checkbox("Color by HP (green to red)##EntityHP", &m_colorByHP);

            if (m_colorByHP) {
                ImGui::TextDisabled("Text color below only sets transparency while this is on.");
            }

            ImGui::ColorEdit4("Text color##EntityHP", &m_textColor.x, ImGuiColorEditFlags_AlphaBar);
            ImGui::Checkbox(isOverlay ? "Text shadow##EntityHP" : "Text outline##EntityHP", &m_textShadow);

            if (isOverlay) {
                ImGui::Spacing();
                ImGui::Checkbox("Background box##EntityHP", &m_showBox);

                if (m_showBox) {
                    ImGui::ColorEdit4("Box color##EntityHP", &m_boxColor.x, ImGuiColorEditFlags_AlphaBar);
                    ImGui::SliderFloat("Box padding##EntityHP", &m_boxPadding, 0.0f, 10.0f, "%.0f px");
                    ImGui::SliderFloat("Box rounding##EntityHP", &m_boxRounding, 0.0f, 10.0f, "%.0f px");
                }
            }

            ImGui::TreePop();
        }
    }

    void EntityHP::onConfigLoad(const Config& cfg) {
        m_isEnabled = cfg.get<bool>("EntityHP.Enabled").value_or(false);
        m_mode = cfg.get<int>("EntityHP.Mode").value_or(MODE_OVERLAY) == MODE_IN_GAME ? MODE_IN_GAME : MODE_OVERLAY;
        m_showPlayers = cfg.get<bool>("EntityHP.Players").value_or(true);
        m_showMonsters = cfg.get<bool>("EntityHP.Monsters").value_or(true);
        m_showMax = cfg.get<bool>("EntityHP.ShowMax").value_or(true);
        m_colorByHP = cfg.get<bool>("EntityHP.ColorByHP").value_or(true);
        m_decimals = cfg.get<int>("EntityHP.Decimals").value_or(1);
        m_offsetY = cfg.get<float>("EntityHP.OffsetY").value_or(30.0f);
        m_fontSize = cfg.get<float>("EntityHP.FontSize").value_or(16.0f);
        m_textShadow = cfg.get<bool>("EntityHP.TextShadow").value_or(true);
        m_showBox = cfg.get<bool>("EntityHP.ShowBox").value_or(false);
        m_boxPadding = cfg.get<float>("EntityHP.BoxPadding").value_or(3.0f);
        m_boxRounding = cfg.get<float>("EntityHP.BoxRounding").value_or(3.0f);
        m_textColor = ImGui::ColorConvertU32ToFloat4(cfg.get<unsigned int>("EntityHP.TextColor").value_or(IM_COL32(255, 255, 255, 255)));
        m_boxColor = ImGui::ColorConvertU32ToFloat4(cfg.get<unsigned int>("EntityHP.BoxColor").value_or(IM_COL32(0, 0, 0, 153)));
    }

    void EntityHP::onConfigSave(Config& cfg) {
        cfg.set<bool>("EntityHP.Enabled", m_isEnabled);
        cfg.set<int>("EntityHP.Mode", m_mode);
        cfg.set<bool>("EntityHP.Players", m_showPlayers);
        cfg.set<bool>("EntityHP.Monsters", m_showMonsters);
        cfg.set<bool>("EntityHP.ShowMax", m_showMax);
        cfg.set<bool>("EntityHP.ColorByHP", m_colorByHP);
        cfg.set<int>("EntityHP.Decimals", m_decimals);
        cfg.set<float>("EntityHP.OffsetY", m_offsetY);
        cfg.set<float>("EntityHP.FontSize", m_fontSize);
        cfg.set<bool>("EntityHP.TextShadow", m_textShadow);
        cfg.set<bool>("EntityHP.ShowBox", m_showBox);
        cfg.set<float>("EntityHP.BoxPadding", m_boxPadding);
        cfg.set<float>("EntityHP.BoxRounding", m_boxRounding);
        cfg.set<unsigned int>("EntityHP.TextColor", ImGui::ColorConvertFloat4ToU32(m_textColor));
        cfg.set<unsigned int>("EntityHP.BoxColor", ImGui::ColorConvertFloat4ToU32(m_boxColor));
    }

    void EntityHP::onCharacterUpdate(uintptr_t character) {
        NameTag tag{};

        if (!readNameTag(character, m_nameRangeLoad, tag) || !(tag.isNPC ? m_showMonsters : m_showPlayers)) {
            return;
        }

        // Drawn now, while the client lays out its name tags for this frame.
        if (m_mode == MODE_IN_GAME && m_canDrawInGame) {
            char text[64];
            wchar_t wideText[64]{};

            formatLabel(tag.life, tag.lifeMax, text, sizeof(text));

            for (size_t i = 0; i < sizeof(text) - 1 && text[i] != '\0'; ++i) {
                wideText[i] = (wchar_t)(unsigned char)text[i];
            }

            drawInGame(tag, wideText, m_offsetY, toARGB(labelColor(tag.life, tag.lifeMax)), m_textShadow);
            return;
        }

        scoped_lock<mutex> _{ m_labelsMutex };

        m_labels[character] = Label{ tag.x, tag.y, tag.life, tag.lifeMax, GetTickCount() };
    }
}
