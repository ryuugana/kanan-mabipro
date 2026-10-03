#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <Windows.h>

#include <imgui.h>

#include <Scan.hpp>

#include "Log.hpp"
#include "CharacterHook.hpp"
#include "EntityHP.hpp"
#include "KCharacter.hpp"
#include "Kanan.hpp"
#include "UIScale.hpp"

using namespace std;

namespace kanan {
    // G13 client layout (Pleione.dll / Standard.dll):
    //   pleione::CCharacter +0x198 -> render entry, render entry +0x0C -> CCharacterSticker (the in-world name)
    //   CCharacterSticker +0x10 layer its texts are drawn at, +0x14 depth, +0x54 camera distance,
    //   +0x58/+0x5A screen x/y, +0x64 chat balloon, +0xA5 enabled
    //   CCharacter vtable +0x50 -> core::IParameter
    namespace offsets {
        constexpr uintptr_t renderEntry = 0x198;
        constexpr uintptr_t sticker = 0x0C;
        constexpr uintptr_t stickerLayer = 0x10;
        constexpr uintptr_t stickerDepth = 0x14;
        constexpr uintptr_t stickerDistance = 0x54;
        constexpr uintptr_t stickerScreenX = 0x58;
        constexpr uintptr_t stickerScreenY = 0x5A;
        constexpr uintptr_t stickerBalloon = 0x64;   // its chat balloon (CBalloon, a CWindow)
        constexpr uintptr_t stickerBalloon2 = 0x68;  // a second balloon-like window (debug log only,
                                                     //   not yet identified)
        constexpr uintptr_t stickerEnabled = 0xA5;
        constexpr uintptr_t getParameter = 0x50;
    }

    using GetFloat = float(__thiscall*)(uintptr_t object);
    using GetBool = bool(__thiscall*)(uintptr_t object);
    using GetPointer = uintptr_t(__thiscall*)(uintptr_t object);

    static GetFloat g_getLife{ nullptr };
    static GetFloat g_getLifeMax{ nullptr };
    static GetBool g_isNPC{ nullptr };

    // pleione::TSingleton<CWindowMgr>::GetInstance - the interface window manager, used to hide HP
    // labels behind open windows the way the game hides name tags.
    using GetWindowMgr = uintptr_t(__cdecl*)();
    static GetWindowMgr g_getWindowMgr{ nullptr };

    namespace windowOffsets {
        constexpr uintptr_t firstWindow = 0x4B4;  // CWindowMgr: first top-level window
        constexpr uintptr_t nextWindow = 0xD4;    // CWindow: next top-level window (0 ends the list)
        constexpr uintptr_t id = 0x08;            // CWindow: id (diagnostics only)
        constexpr uintptr_t rectX1 = 0x44;        // CWindow: pick-area rect x1, y1, x2, y2 (int16,
        constexpr uintptr_t rectY1 = 0x46;        //   the interface's layout pixels)
        constexpr uintptr_t rectX2 = 0x48;
        constexpr uintptr_t rectY2 = 0x4A;
        constexpr uintptr_t color0 = 0x50;        // CWindow: background gradient colors (0xAARRGGBB)
        constexpr uintptr_t color1 = 0x54;
        constexpr uintptr_t visible = 0x99;       // CWindow::IsVisible
        constexpr uintptr_t baseImage = 0x9C;     // CWindow: base-panel image handle (0 if none)
    }

    // Chat balloon (CBalloon, a CWindow over a character's head). Its pick rect (+0x44) is never
    // filled in, so its place is read from its position and size instead (int16, the name tags'
    // pixels). Seen live: over a name tag at x 429, a 61x28 balloon at x 427 and a 196x52 one at
    // x 407 (the balloon extends right from its tail at the bottom left), bottom 8 pixels above the tag.
    namespace balloonOffsets {
        constexpr uintptr_t left = 0x3C;          // CBalloon: left edge
        constexpr uintptr_t top = 0x3E;           // CBalloon: top edge
        constexpr uintptr_t width = 0x40;
        constexpr uintptr_t height = 0x42;

        // The speaker's name is drawn just above the balloon, outside its rect: about one line of
        // name tag text (estimated, in the name tags' pixels).
        constexpr int16_t nameAbove = 16;
    }

    // Windows with no background of their own that still cover what's behind them, because what
    // they draw is solid. Every other window hides HP labels only if it has a background.
    // Matched exactly against each window's RTTI class name.
    static const char* const g_solidHudWindows[] = {
        ".?AVCMiniMapView@pleione@@",       // minimap
        ".?AVCGameClockView@pleione@@",     // in-game clock
        ".?AVCMissionSheetView@pleione@@",  // quest details (the open quest scroll)
    };

    static bool isSolidHud(const char* className) {
        for (auto name : g_solidHudWindows) {
            if (strcmp(className, name) == 0) {
                return true;
            }
        }

        return false;
    }

    // Reads a window's RTTI class name (e.g. ".?AVCMiniMapView@pleione@@") into OUT.
    // Kept free of C++ objects so the SEH guard can protect the raw reads.
    static void readClassName(uintptr_t window, char* out, int size) {
        out[0] = '\0';

        __try {
            auto vtable = *(uintptr_t*)window;
            auto locator = *(uintptr_t*)(vtable - 4);     // RTTI complete object locator
            auto type = *(uintptr_t*)(locator + 12);      // its type descriptor
            auto name = (const char*)(type + 8);          // the decorated class name

            int i = 0;

            for (; i < size - 1 && name[i] != '\0'; ++i) {
                out[i] = name[i];
            }

            out[i] = '\0';
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            out[0] = '\0';
        }
    }

    struct WindowInfo {
        float x1, y1, x2, y2;   // real pixels
        bool occludes;          // HP labels behind it are hidden
        bool transparent;       // no background color or base-panel image
        uint32_t id;
        char className[64];
    };

    // Reads the visible top-level windows and decides, for each, whether HP labels hide behind it:
    // windows with a background (a color with alpha, or a base-panel image) do, as do the few
    // background-less ones in g_solidHudWindows. Everything else is an empty layout container or
    // event frame (top menu bars, arena/contest/countdown frames) that the world shows through.
    // Kept free of C++ objects so the SEH guard can protect the raw reads.
    static int readWindows(float scale, WindowInfo* out, int maxOut) {
        int n = 0;

        if (g_getWindowMgr == nullptr) {
            return 0;
        }

        __try {
            auto mgr = g_getWindowMgr();

            if (mgr == 0) {
                return 0;
            }

            auto window = *(uintptr_t*)(mgr + windowOffsets::firstWindow);

            for (int i = 0; i < 256 && window != 0 && n < maxOut; ++i) {
                auto current = window;

                window = *(uintptr_t*)(current + windowOffsets::nextWindow);

                if (*(uint8_t*)(current + windowOffsets::visible) == 0) {
                    continue;
                }

                auto x1 = *(int16_t*)(current + windowOffsets::rectX1);
                auto y1 = *(int16_t*)(current + windowOffsets::rectY1);
                auto x2 = *(int16_t*)(current + windowOffsets::rectX2);
                auto y2 = *(int16_t*)(current + windowOffsets::rectY2);

                if (x2 <= x1 || y2 <= y1) {
                    continue;
                }

                auto& info = out[n];

                info.x1 = x1 * scale;
                info.y1 = y1 * scale;
                info.x2 = x2 * scale;
                info.y2 = y2 * scale;
                info.id = *(uint32_t*)(current + windowOffsets::id);

                auto c0 = *(uint32_t*)(current + windowOffsets::color0);
                auto c1 = *(uint32_t*)(current + windowOffsets::color1);
                auto alpha = (c0 >> 24) > (c1 >> 24) ? (c0 >> 24) : (c1 >> 24);

                info.transparent = alpha < 0x20 && *(uint32_t*)(current + windowOffsets::baseImage) == 0;

                readClassName(current, info.className, sizeof(info.className));

                info.occludes = !info.transparent || isSolidHud(info.className);
                ++n;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        return n;
    }

    // Chat balloons (the speech bubbles over characters) hide HP labels too. Each one belongs to a
    // character's name-tag sticker, which creates it (the one caller of CBalloon::Create). CBalloon is
    // a CWindow, so it has the window fields above.

    // The name-tag sticker of CHARACTER, or 0. SEH-guarded.
    static uintptr_t stickerOf(uintptr_t character) {
        __try {
            auto entry = *(uintptr_t*)(character + offsets::renderEntry);

            return entry != 0 ? *(uintptr_t*)(entry + offsets::sticker) : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    // Reads the rect of the chat balloon BALLOON into RECT (x1, y1, x2, y2), when it's up.
    // SEH-guarded.
    static bool readBalloonRect(uintptr_t balloon, int16_t* rect) {
        __try {
            if (balloon == 0 || *(uint8_t*)(balloon + windowOffsets::visible) == 0) {
                return false;
            }

            auto left = *(int16_t*)(balloon + balloonOffsets::left);
            auto top = *(int16_t*)(balloon + balloonOffsets::top);
            auto width = *(int16_t*)(balloon + balloonOffsets::width);
            auto height = *(int16_t*)(balloon + balloonOffsets::height);

            if (width <= 0 || height <= 0) {
                return false;
            }

            rect[0] = left;
            rect[1] = (int16_t)(top - balloonOffsets::nameAbove);
            rect[2] = (int16_t)(rect[0] + width);
            rect[3] = (int16_t)(top + height);

            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // The chat balloon of CHARACTER, when it's up: its rect, in the same pixels as the name tags.
    static bool readChatBalloon(uintptr_t character, int16_t* rect) {
        auto sticker = stickerOf(character);

        if (sticker == 0) {
            return false;
        }

        uintptr_t balloon = 0;

        __try {
            balloon = *(uintptr_t*)(sticker + offsets::stickerBalloon);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }

        return readBalloonRect(balloon, rect);
    }

    // DEBUG LOG: CHARACTER's chat balloon fields, raw. False when it has no balloon. SEH-guarded.
    static bool readBalloonRaw(uintptr_t character, int16_t& anchorX, int16_t& anchorY, uint8_t& created,
        uint8_t& visible, int16_t* window, int16_t* balloon)
    {
        auto sticker = stickerOf(character);

        if (sticker == 0) {
            return false;
        }

        __try {
            auto b = *(uintptr_t*)(sticker + offsets::stickerBalloon);

            if (b == 0) {
                return false;
            }

            anchorX = *(int16_t*)(sticker + offsets::stickerScreenX);
            anchorY = *(int16_t*)(sticker + offsets::stickerScreenY);
            created = *(uint8_t*)(b + 0x98);    // CBalloon: set once Create has run
            visible = *(uint8_t*)(b + windowOffsets::visible);

            for (int k = 0; k < 8; ++k) {
                window[k] = *(int16_t*)(b + 0x3C + k * 2);
                balloon[k] = *(int16_t*)(b + 0xEC + k * 2);
            }

            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    struct NameTag {
        float x;
        float y;
        float life;
        float lifeMax;
        bool isNPC;
    };

    // MabiPro's Bexon.dll scales the interface ("Interface size") and, separately, the name tags and
    // other world overlays ("World overlay size", 0 = as the interface), and saves both as options.
    // For the overlays it shrinks the interface's layout by interface / overlay while
    // CCharacterSticker::PreRender runs, so a name tag's screen position is in pixels of the
    // overlay size, then draws the tags at that size. Returns the size in percent, or 0.
    static long readBexonOption(const wchar_t* name) {
        wchar_t text[16]{};
        DWORD number{};
        DWORD type{};
        DWORD size{ sizeof(text) };

        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Nexon\\Mabinogi", name, RRF_RT_REG_SZ | RRF_RT_REG_DWORD, &type,
            text, &size) != ERROR_SUCCESS) {
            return 0;
        }

        if (type == REG_DWORD) {
            memcpy(&number, text, sizeof(number));
            return (long)number;
        }

        return wcstol(text, nullptr, 10);
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
        m_showPlayers{ true },
        m_showMonsters{ true },
        m_showMax{ true },
        m_colorByHP{ true },
        m_textShadow{ true },
        m_showBox{ false },
        m_decimals{ 1 },
        m_offsetY{ -20.0f },
        m_fontSize{ 16.0f },
        m_boxPadding{ 3.0f },
        m_boxRounding{ 3.0f },
        m_textColor{ 1.0f, 1.0f, 1.0f, 1.0f },
        m_boxColor{ 0.0f, 0.0f, 0.0f, 0.6f },
        m_isHooked{ false },
        m_nameRangeLoad{ 0 },
        m_tagScale{ 1.0f },
        m_interfaceScale{ 1.0f },
        m_scaleTick{ 0 },
        m_debugLog{ false },
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

        // The interface window manager, to hide labels behind open windows.
        auto pleione = GetModuleHandleA("Pleione.dll");

        if (pleione != nullptr) {
            g_getWindowMgr = (GetWindowMgr)GetProcAddress(pleione,
                "?GetInstance@?$TSingleton@VCWindowMgr@pleione@@@esl@@SAAAVCWindowMgr@pleione@@XZ");
        }

        if (g_getWindowMgr == nullptr) {
            log("[EntityHP] Failed to find the window manager, labels will not hide behind windows");
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

    // The sizes name tags and the interface are drawn at, from Bexon's options (checked once a
    // second) or Kanan's UI Scale.
    void EntityHP::updateScales() {
        auto now = GetTickCount();

        // Checked often so a size change in game is picked up quickly; the cached label positions
        // are in the old scale's pixels, so they must not be drawn against a new scale.
        if (m_scaleTick != 0 && now - m_scaleTick < 250) {
            return;
        }

        m_scaleTick = now == 0 ? 1 : now;

        auto newInterface = 1.0f;
        auto newTag = 1.0f;

        if (GetModuleHandleA("Bexon.dll") == nullptr) {
            newInterface = newTag = UIScale::current();
        }
        else {
            auto interfaceSize = readBexonOption(L"MabiProInterfaceScale");
            auto overlaySize = readBexonOption(L"MabiProOverlayScale");

            if (interfaceSize < 50 || interfaceSize > 1000) {
                interfaceSize = 100;
            }

            // 0 is "as the interface".
            if (overlaySize < 50 || overlaySize > 1000) {
                overlaySize = interfaceSize;
            }

            newInterface = interfaceSize / 100.0f;
            newTag = overlaySize / 100.0f;
        }

        // When the size changes, the client rebuilds its name tags: the cached positions are stale
        // (in the old scale) until the character updates come in again. Drop them so nothing is
        // drawn frozen at the wrong place; they refill within a frame or two.
        if (newTag != m_tagScale || newInterface != m_interfaceScale) {
            scoped_lock<mutex> _{ m_labelsMutex };

            m_labels.clear();
            m_balloons.clear();
        }

        m_interfaceScale = newInterface;
        m_tagScale = newTag;
    }

    void EntityHP::onFrame() {
        if (!m_isEnabled) {
            scoped_lock<mutex> _{ m_labelsMutex };

            m_labels.clear();
            m_balloons.clear();
            return;
        }

        updateScales();

        auto& io = ImGui::GetIO();
        auto drawList = ImGui::GetBackgroundDrawList();
        auto font = ImGui::GetFont();
        auto now = GetTickCount();
        // Name tag positions (and the height above them) are in the name tags' scaled pixels.
        auto scale = m_tagScale;
        char text[64];

        // What hides labels, in real pixels: open interface windows (whose rects are in the interface's
        // layout pixels, scaled by the interface size) and chat balloons (in the name tags' pixels,
        // scaled like the labels).
        WindowInfo windows[64];
        auto windowCount = readWindows(m_interfaceScale, windows, _countof(windows));

        struct Occluder {
            float x1, y1, x2, y2;
        };

        Occluder occluders[128];
        int occluderCount = 0;

        for (int w = 0; w < windowCount && occluderCount < _countof(occluders); ++w) {
            if (windows[w].occludes) {
                occluders[occluderCount++] = Occluder{ windows[w].x1, windows[w].y1, windows[w].x2, windows[w].y2 };
            }
        }

        if (m_debugLog) {
            static DWORD s_debugTick = 0;

            if (now - s_debugTick > 1000) {
                s_debugTick = now;

                for (int w = 0; w < windowCount; ++w) {
                    log("[EntityHP][win] %s id=%u %s%s rect %.0f,%.0f-%.0f,%.0f", windows[w].occludes ? "HIDES" : "over ",
                        windows[w].id, windows[w].className, windows[w].transparent ? " (transparent)" : "",
                        windows[w].x1, windows[w].y1, windows[w].x2, windows[w].y2);
                }

                scoped_lock<mutex> _{ m_labelsMutex };

                for (auto it = m_balloonDebug.begin(); it != m_balloonDebug.end();) {
                    auto& r = it->second;

                    if (now - r.tick > 1000) {
                        it = m_balloonDebug.erase(it);
                        continue;
                    }

                    log("[EntityHP][balloon] char %p anchor %d,%d created %u visible %u | +3C %d %d %d %d %d %d %d %d"
                        " | +EC %d %d %d %d %d %d %d %d (scale %.2f)",
                        (void*)it->first, r.anchorX, r.anchorY, r.created, r.visible,
                        r.window[0], r.window[1], r.window[2], r.window[3], r.window[4], r.window[5], r.window[6], r.window[7],
                        r.balloon[0], r.balloon[1], r.balloon[2], r.balloon[3], r.balloon[4], r.balloon[5], r.balloon[6],
                        r.balloon[7], scale);
                    ++it;
                }
            }
        }

        scoped_lock<mutex> _{ m_labelsMutex };

        for (auto it = m_balloons.begin(); it != m_balloons.end();) {
            // Balloons whose character stopped updating, or that went away, drop off.
            if (now - it->second.tick > 250) {
                it = m_balloons.erase(it);
                continue;
            }

            if (occluderCount < _countof(occluders)) {
                auto& b = it->second;

                occluders[occluderCount++] = Occluder{ b.x1 * scale, b.y1 * scale, b.x2 * scale, b.y2 * scale };
            }

            ++it;
        }

        for (auto it = m_labels.begin(); it != m_labels.end();) {
            auto& label = it->second;

            // Characters that stopped updating (despawned, out of range) drop off.
            if (now - label.tick > 250) {
                it = m_labels.erase(it);
                continue;
            }

            ++it;

            auto x = label.x * scale;
            auto y = label.y * scale;

            if (x < -50.0f || y < -50.0f || x > io.DisplaySize.x + 50.0f || y > io.DisplaySize.y + 50.0f) {
                continue;
            }

            formatLabel(label.life, label.lifeMax, text, sizeof(text));

            auto color = labelColor(label.life, label.lifeMax);
            auto size = font->CalcTextSizeA(m_fontSize, FLT_MAX, 0.0f, text);
            ImVec2 pos{ x - size.x / 2.0f, y - m_offsetY * scale - size.y };

            // Draws the label, optionally clipped to a rectangle (for the parts not behind a window).
            auto emit = [&](float clx1, float cly1, float clx2, float cly2, bool clip) {
                if (clip && (clx2 <= clx1 || cly2 <= cly1)) {
                    return;
                }

                if (clip) {
                    drawList->PushClipRect(ImVec2{ clx1, cly1 }, ImVec2{ clx2, cly2 }, true);
                }

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

                if (clip) {
                    drawList->PopClipRect();
                }
            };

            // The part of the label covered by windows and balloons (union of the overlaps, as one box).
            auto covered = false;
            auto cx1 = FLT_MAX, cy1 = FLT_MAX, cx2 = -FLT_MAX, cy2 = -FLT_MAX;

            for (int o = 0; o < occluderCount; ++o) {
                auto& r = occluders[o];
                auto ix1 = pos.x > r.x1 ? pos.x : r.x1;
                auto iy1 = pos.y > r.y1 ? pos.y : r.y1;
                auto ix2 = (pos.x + size.x) < r.x2 ? (pos.x + size.x) : r.x2;
                auto iy2 = (pos.y + size.y) < r.y2 ? (pos.y + size.y) : r.y2;

                if (ix2 > ix1 && iy2 > iy1) {
                    covered = true;
                    cx1 = cx1 < ix1 ? cx1 : ix1;
                    cy1 = cy1 < iy1 ? cy1 : iy1;
                    cx2 = cx2 > ix2 ? cx2 : ix2;
                    cy2 = cy2 > iy2 ? cy2 : iy2;
                }
            }

            if (!covered) {
                emit(0, 0, 0, 0, false);
                continue;
            }

            // Draw the strips of the label that stick out of the covered box (so it shows partially).
            auto bx2 = pos.x + size.x;
            auto by2 = pos.y + size.y;
            auto bandTop = pos.y > cy1 ? pos.y : cy1;
            auto bandBottom = by2 < cy2 ? by2 : cy2;

            emit(pos.x, pos.y, bx2, cy1, true);                 // above the window
            emit(pos.x, cy2, bx2, by2, true);                   // below the window
            emit(pos.x, bandTop, cx1, bandBottom, true);        // left of the window
            emit(cx2, bandTop, bx2, bandBottom, true);          // right of the window
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
            ImGui::TextDisabled("Drawn by Kanan on top of everything, with its own font, size and box.");

            ImGui::Checkbox("Players##EntityHP", &m_showPlayers);
            ImGui::Checkbox("NPCs and monsters##EntityHP", &m_showMonsters);
            ImGui::Checkbox("Show max HP##EntityHP", &m_showMax);
            ImGui::SliderInt("Decimals##EntityHP", &m_decimals, 0, 2);
            ImGui::SliderFloat("Height above name##EntityHP", &m_offsetY, -150.0f, 150.0f, "%.0f px");
            ImGui::SliderFloat("Text size##EntityHP", &m_fontSize, 10.0f, 32.0f, "%.0f");

            ImGui::Spacing();
            ImGui::Checkbox("Color by HP (green to red)##EntityHP", &m_colorByHP);

            if (m_colorByHP) {
                ImGui::TextDisabled("Text color below only sets transparency while this is on.");
            }

            ImGui::ColorEdit4("Text color##EntityHP", &m_textColor.x, ImGuiColorEditFlags_AlphaBar);
            ImGui::Checkbox("Text shadow##EntityHP", &m_textShadow);

            ImGui::Spacing();
            ImGui::Checkbox("Background box##EntityHP", &m_showBox);

            if (m_showBox) {
                ImGui::ColorEdit4("Box color##EntityHP", &m_boxColor.x, ImGuiColorEditFlags_AlphaBar);
                ImGui::SliderFloat("Box padding##EntityHP", &m_boxPadding, 0.0f, 10.0f, "%.0f px");
                ImGui::SliderFloat("Box rounding##EntityHP", &m_boxRounding, 0.0f, 10.0f, "%.0f px");
            }

            ImGui::TreePop();
        }
    }

    void EntityHP::onConfigLoad(const Config& cfg) {
        m_isEnabled = cfg.get<bool>("EntityHP.Enabled").value_or(false);
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
        m_debugLog = cfg.get<bool>("EntityHP.DebugLog").value_or(false);
    }

    void EntityHP::onConfigSave(Config& cfg) {
        cfg.set<bool>("EntityHP.Enabled", m_isEnabled);
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
        cfg.set<bool>("EntityHP.DebugLog", m_debugLog);
    }

    void EntityHP::onCharacterUpdate(uintptr_t character) {
        // Any character's chat balloon hides labels, whether or not that character shows HP.
        int16_t balloon[4]{};

        if (readChatBalloon(character, balloon)) {
            scoped_lock<mutex> _{ m_labelsMutex };

            m_balloons[character] = Balloon{ (float)balloon[0], (float)balloon[1], (float)balloon[2], (float)balloon[3],
                GetTickCount() };
        }

        if (m_debugLog) {
            BalloonRaw r{};

            if (readBalloonRaw(character, r.anchorX, r.anchorY, r.created, r.visible, r.window, r.balloon)) {
                r.tick = GetTickCount();

                scoped_lock<mutex> _{ m_labelsMutex };

                m_balloonDebug[character] = r;
            }
        }

        NameTag tag{};

        if (!readNameTag(character, m_nameRangeLoad, tag) || !(tag.isNPC ? m_showMonsters : m_showPlayers)) {
            return;
        }

        scoped_lock<mutex> _{ m_labelsMutex };

        m_labels[character] = Label{ tag.x, tag.y, tag.life, tag.lifeMax, GetTickCount() };
    }
}
