#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>

#include <imgui.h>

#include <Scan.hpp>
#include <String.hpp>

#include "Log.hpp"
#include "TimeAlarm.hpp"

using namespace std;

namespace kanan {
    // core (Standard.dll)
    using GetGlobalTimeFn = uint64_t(__cdecl*)();
    using GlobalTimeToGameTimeFn = void(__cdecl*)(uint64_t time, unsigned long* day, unsigned long* hour, unsigned long* minute);
    using ShowCaptionFn = void(__cdecl*)(uint64_t id, const void* text, int style, unsigned long time1, unsigned long time2, unsigned long color, uint64_t unknown);
    using LocalizerInstanceFn = void*(__cdecl*)();
    using GetLocalTextFn = void*(__thiscall*)(void* localizer, void* formatter, const void* key);
    // esl (ESL.dll)
    using StringCtorFn = void*(__thiscall*)(void* str, const wchar_t* text);
    using StringDtorFn = void(__thiscall*)(void* str);
    using FormatterToStringFn = void*(__thiscall*)(void* formatter, void* str);
    using FormatterDtorFn = void(__thiscall*)(void* formatter);

    static GetGlobalTimeFn g_getGlobalTime{ nullptr };
    static GlobalTimeToGameTimeFn g_globalTimeToGameTime{ nullptr };
    static ShowCaptionFn g_showCaption{ nullptr };
    static LocalizerInstanceFn g_localizerInstance{ nullptr };
    static GetLocalTextFn g_getLocalText{ nullptr };
    static StringCtorFn g_stringCtor{ nullptr };
    static StringDtorFn g_stringDtor{ nullptr };
    static FormatterToStringFn g_formatterToString{ nullptr };
    static FormatterDtorFn g_formatterDtor{ nullptr };

    static TimeAlarm* g_timeAlarm{ nullptr };

    // The code the hook replaces: push <contents id> / call core::IServiceMgr::IsUsableContents.
    static uint32_t g_contentsId{ 0 };
    static uintptr_t g_isUsableContents{ 0 };
    static uintptr_t g_alarmReturn{ 0 };

    // The arguments the game gives stdapi_ShowCaption for the Nao message it shows here.
    static const uint64_t CAPTION_ID = 0x3000000000000000ull;
    static const unsigned long CAPTION_TIME = 5000;

    static const char* STYLES =
        "1: Scrolling message (white)\0"
        "2: Scrolling message (red)\0"
        "3: Center of the screen\0"
        "4: Bottom center\0"
        "5: Left center (like weapon swap)\0"
        "6: Scrolling message (green)\0"
        "7: Center of the screen + system chat message\0"
        "8: Scrolling message (green, second style)\0"
        "9: Center of the screen, blinking 5 times\0";

    static void __cdecl onAlarmUpdate() {
        if (g_timeAlarm != nullptr) {
            g_timeAlarm->onGameUpdate();
        }
    }

    static __declspec(naked) void hookGameUpdate() {
        __asm {
            pushad
            pushfd
            call    onAlarmUpdate
            popfd
            popad
            push    g_contentsId
            call    g_isUsableContents
            jmp     g_alarmReturn
        }
    }

    // AstralWorld's alarm window: from four game minutes before the set time up to the set time, with
    // the same comparisons (including their quirks around midnight).
    static bool isAlarmTime(int alarmHour, int alarmMinute, unsigned long hour, unsigned long minute) {
        auto min1 = (unsigned long)((alarmMinute - 4) % 60);
        auto min2 = (unsigned long)(alarmMinute % 60);
        auto hour1 = (unsigned long)((alarmHour - alarmMinute / 60) % 24);
        auto hour2 = (unsigned long)((alarmHour + alarmMinute / 60) % 24);

        if (alarmMinute - 4 < 0) {
            hour1 = (unsigned long)(alarmHour - 1);
            min1 = (unsigned long)(60 + alarmMinute - 4);
        }

        // Every hour.
        if (alarmHour == 24) {
            if (min1 > min2) {
                return minute > min1 || minute < min2;
            }

            return minute >= min1 && minute <= min2;
        }

        if (hour1 > hour2) {
            if (hour == hour2) {
                return minute <= min2;
            }

            if (hour == hour1) {
                return minute >= min1;
            }

            return false;
        }

        if (hour1 == hour2) {
            return hour == hour1 && minute >= min1 && minute <= min2;
        }

        if (hour == hour1) {
            return minute >= min1;
        }

        if (hour == hour2) {
            return minute <= min2;
        }

        return false;
    }

    // Whether the text is safe to use as a printf format with the hour and minute: at most two
    // number fields and nothing else.
    static bool isTimeFormat(const wstring& text) {
        int fields = 0;

        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] != L'%') {
                continue;
            }

            if (++i < text.size() && text[i] == L'%') {
                continue;
            }

            while (i < text.size() && wcschr(L"-+ #0", text[i]) != nullptr) {
                ++i;
            }

            while (i < text.size() && iswdigit(text[i])) {
                ++i;
            }

            if (i >= text.size() || wcschr(L"diuxX", text[i]) == nullptr || ++fields > 2) {
                return false;
            }
        }

        return true;
    }

    TimeAlarm::TimeAlarm()
        : m_enabled{ false },
        m_isAvailable{ false },
        m_alarms{},
        m_patch{}
    {
        log("[TimeAlarm] Entering constructor...");

        for (auto& alarm : m_alarms) {
            alarm = Alarm{ false, "", 24, 0, 7, false };
        }

        auto standard = GetModuleHandleA("Standard.dll");
        auto esl = GetModuleHandleA("ESL.dll");

        if (standard == nullptr || esl == nullptr) {
            log("[TimeAlarm] Game modules are not loaded.");
            log("[TimeAlarm] Leaving constructor.");
            return;
        }

        g_getGlobalTime = (GetGlobalTimeFn)GetProcAddress(standard, "?stdapi_GetGlobalTime@core@@YA_KXZ");
        g_globalTimeToGameTime = (GlobalTimeToGameTimeFn)GetProcAddress(standard, "?stdapi_GlobalTimeToGameTime@core@@YAX_KAAK11@Z");
        g_showCaption = (ShowCaptionFn)GetProcAddress(standard, "?stdapi_ShowCaption@core@@YAX_KABV?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@W4EMessageCaptionType@@KKK0@Z");
        g_localizerInstance = (LocalizerInstanceFn)GetProcAddress(standard, "?Instance@CLocalizer@core@@SAAAV12@XZ");
        g_getLocalText = (GetLocalTextFn)GetProcAddress(standard, "?GetLocalText@CLocalizer@core@@QBE?AVCFormatter@esl@@ABV?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@4@@Z");
        g_stringCtor = (StringCtorFn)GetProcAddress(esl, "??0?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QAE@PB_W@Z");
        g_stringDtor = (StringDtorFn)GetProcAddress(esl, "??1?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QAE@XZ");
        g_formatterToString = (FormatterToStringFn)GetProcAddress(esl, "??BCFormatter@esl@@QBE?AV?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@1@XZ");
        g_formatterDtor = (FormatterDtorFn)GetProcAddress(esl, "??1CFormatter@esl@@QAE@XZ");
        auto isUsableContents = (uintptr_t)GetProcAddress(standard, "?IsUsableContents@IServiceMgr@core@@QBE_NW4EServiceContents@2@@Z");

        if (g_getGlobalTime == nullptr || g_globalTimeToGameTime == nullptr || g_showCaption == nullptr ||
            g_localizerInstance == nullptr || g_getLocalText == nullptr || g_stringCtor == nullptr ||
            g_stringDtor == nullptr || g_formatterToString == nullptr || g_formatterDtor == nullptr || isUsableContents == 0) {
            log("[TimeAlarm] Failed to find the game functions.");
            log("[TimeAlarm] Leaving constructor.");
            return;
        }

        // The Nao support check in the game's update, where AstralWorld hooked in:
        //   mov ecx, [esi+120h] / push 14h / nop / call IServiceMgr::IsUsableContents /
        //   mov edi, 1388h / test al, al / jne
        auto address = scan("Pleione.dll", "8B 8E 20 01 00 00 6A 14 90 E8 ? ? ? ? BF 88 13 00 00 84 C0 75");

        if (!address) {
            log("[TimeAlarm] Failed to find the game update.");
            log("[TimeAlarm] Leaving constructor.");
            return;
        }

        auto site = *address + 6;
        auto call = site + 3;

        if (call + 5 + *(int32_t*)(call + 1) != isUsableContents) {
            log("[TimeAlarm] Unexpected code at %p, not patching.", site);
            log("[TimeAlarm] Leaving constructor.");
            return;
        }

        g_contentsId = *(uint8_t*)(site + 1);
        g_isUsableContents = isUsableContents;
        g_alarmReturn = site + 8;

        auto rel = (uintptr_t)&hookGameUpdate - (site + 5);

        m_patch.address = site;
        m_patch.bytes = { 0xE9, (int16_t)(rel & 0xFF), (int16_t)((rel >> 8) & 0xFF), (int16_t)((rel >> 16) & 0xFF), (int16_t)((rel >> 24) & 0xFF), 0x90, 0x90, 0x90 };
        m_isAvailable = true;
        g_timeAlarm = this;

        log("[TimeAlarm] Found the game update at %p", site);
        log("[TimeAlarm] Leaving constructor.");
    }

    TimeAlarm::~TimeAlarm() {
        if (g_timeAlarm == this) {
            g_timeAlarm = nullptr;
        }
    }

    void TimeAlarm::onGameUpdate() {
        if (!m_enabled) {
            return;
        }

        unsigned long day{}, hour{}, minute{};

        g_globalTimeToGameTime(g_getGlobalTime(), &day, &hour, &minute);

        for (auto& alarm : m_alarms) {
            if (!alarm.enabled) {
                continue;
            }

            if (isAlarmTime(alarm.hour, alarm.minute, hour, minute)) {
                if (!alarm.hasRung) {
                    alarm.hasRung = true;
                    show(alarm, hour, minute);
                }
            }
            else {
                alarm.hasRung = false;
            }
        }
    }

    void TimeAlarm::show(const Alarm& alarm, unsigned long hour, unsigned long minute) {
        auto text = widen(alarm.text);
        wchar_t message[512]{};

        if (isTimeFormat(text)) {
            swprintf_s(message, text.c_str(), (hour + (minute + 3) / 60) % 24, (minute + 3) % 60);
        }
        else {
            wcsncpy_s(message, text.c_str(), _TRUNCATE);
        }

        log("[TimeAlarm] Alarm at %02lu:%02lu", hour, minute);

        // Same path as the game's own message: the text goes through the localizer and formatter,
        // then to stdapi_ShowCaption. esl::CStringT is a single pointer; the formatter is small.
        uintptr_t key[4]{};
        uintptr_t caption[4]{};
        alignas(16) uint8_t formatter[64]{};

        g_stringCtor(key, message);
        g_getLocalText(g_localizerInstance(), formatter, key);
        g_formatterToString(formatter, caption);
        g_showCaption(CAPTION_ID, caption, alarm.style, CAPTION_TIME, CAPTION_TIME, 0xFFFFFFFF, 0);
        g_stringDtor(caption);
        g_formatterDtor(formatter);
        g_stringDtor(key);
    }

    void TimeAlarm::apply() {
        if (!m_isAvailable) {
            return;
        }

        log("[TimeAlarm] Toggling %s", m_enabled ? "on" : "off");

        if (m_enabled) {
            patch(m_patch);
        }
        else {
            undoPatch(m_patch);

            for (auto& alarm : m_alarms) {
                alarm.hasRung = false;
            }
        }
    }

    void TimeAlarm::onUI() {
        if (ImGui::TreeNode("Time Alarm")) {
            if (!m_isAvailable) {
                ImGui::TextWrapped("Not available for this version of the game.");
                ImGui::TreePop();
                return;
            }

            ImGui::TextWrapped(
                "Shows a message on screen at set in-game (Erinn) times. An alarm goes off once, "
                "during the four game minutes before its time. Set the hour to 24 to be alerted every hour. "
                "The message can show the time with %%02d:%%02d (hour, then minute)."
            );
            ImGui::Spacing();

            if (ImGui::Checkbox("Enable Time Alarms", &m_enabled)) {
                apply();
            }

            for (int i = 0; i < (int)m_alarms.size(); ++i) {
                auto& alarm = m_alarms[i];
                char label[32]{};

                sprintf_s(label, "Alarm %d%s", i + 1, alarm.enabled ? " (on)" : "");

                ImGui::PushID(i);

                if (ImGui::TreeNode("alarm", "%s", label)) {
                    ImGui::Checkbox("On", &alarm.enabled);
                    ImGui::InputText("Message", alarm.text, sizeof(alarm.text));

                    if (ImGui::InputInt("Hour (24 = every hour)", &alarm.hour)) {
                        alarm.hour = std::clamp(alarm.hour, 0, 24);
                        alarm.hasRung = false;
                    }

                    if (ImGui::InputInt("Minute", &alarm.minute)) {
                        alarm.minute = std::clamp(alarm.minute, 0, 60);
                        alarm.hasRung = false;
                    }

                    int style = alarm.style - 1;

                    if (ImGui::Combo("Style", &style, STYLES)) {
                        alarm.style = style + 1;
                    }

                    ImGui::TreePop();
                }

                ImGui::PopID();
            }

            ImGui::TreePop();
        }
    }

    void TimeAlarm::onConfigLoad(const Config& cfg) {
        m_enabled = cfg.get<bool>("TimeAlarm.Enabled").value_or(false);

        for (int i = 0; i < (int)m_alarms.size(); ++i) {
            auto& alarm = m_alarms[i];
            auto key = "TimeAlarm.Alarm" + to_string(i + 1) + ".";

            alarm.enabled = cfg.get<bool>(key + "Enabled").value_or(false);
            strncpy_s(alarm.text, cfg.get(key + "Text").value_or("").c_str(), _TRUNCATE);
            alarm.hour = std::clamp(cfg.get<int>(key + "Hour").value_or(24), 0, 24);
            alarm.minute = std::clamp(cfg.get<int>(key + "Minute").value_or(0), 0, 60);
            alarm.style = cfg.get<int>(key + "Style").value_or(7);
            alarm.hasRung = false;

            // AstralWorld meant to use style 3 for anything outside 1-9.
            if (alarm.style < 1 || alarm.style > 9) {
                alarm.style = 3;
            }
        }

        if (m_enabled) {
            apply();
        }
    }

    void TimeAlarm::onConfigSave(Config& cfg) {
        cfg.set<bool>("TimeAlarm.Enabled", m_enabled);

        for (int i = 0; i < (int)m_alarms.size(); ++i) {
            auto& alarm = m_alarms[i];
            auto key = "TimeAlarm.Alarm" + to_string(i + 1) + ".";

            cfg.set<bool>(key + "Enabled", alarm.enabled);
            cfg.set(key + "Text", alarm.text);
            cfg.set<int>(key + "Hour", alarm.hour);
            cfg.set<int>(key + "Minute", alarm.minute);
            cfg.set<int>(key + "Style", alarm.style);
        }
    }
}
