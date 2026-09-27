#pragma once

#include <array>
#include <cstdint>

#include <Patch.hpp>

#include "Mod.hpp"

namespace kanan {
    // Shows a message on screen at set in-game times (up to 10 alarms), for example before
    // Erinn midnight. Ported from AstralWorld's Time Alarm (Noginogi alarm): TimeAlarm and
    // Alarm1..10_Using/_Text/_Hour/_Min/_Code in mss32.ini.
    //
    // An alarm rings once while the game time is within the four game minutes up to its set time.
    // Hour 24 means every hour. The text may contain up to two number fields such as %02d:%02d,
    // which are filled in with the game hour and minute three minutes ahead, like AstralWorld did.
    //
    // AstralWorld took over the game's "Nao support recharged" message code to show the alarm; this
    // only runs its check at the same point in the game's update and shows the message itself
    // (core::stdapi_ShowCaption with the same arguments), leaving the Nao message untouched.
    class TimeAlarm : public Mod {
    public:
        TimeAlarm();
        ~TimeAlarm();

        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

        // Called by the hook in the game's update.
        void onGameUpdate();

    private:
        struct Alarm {
            bool enabled;
            char text[256];
            int hour;
            int minute;
            int style;
            bool hasRung;
        };

        bool m_enabled;
        bool m_isAvailable;
        std::array<Alarm, 10> m_alarms;
        Patch m_patch;

        void apply();
        void show(const Alarm& alarm, unsigned long hour, unsigned long minute);
    };
}
