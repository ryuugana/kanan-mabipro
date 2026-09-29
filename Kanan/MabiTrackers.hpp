#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "Mod.hpp"

namespace kanan {
    // Erinn Tracker: a page with the weather forecast for every region with weather, and the
    // schedules of the NPCs that come and go (Price, Rua, Fleta, Tarlach) and the moon gates, opened
    // with .tracker or from the Kanan menu. The NPC schedules come from the server's scripts and the
    // moon gates from its moon gate code (see MabiTrackers.html); they only need the game clock,
    // which the page gets from here, and the moon gate rotation, read from the game (core::CGateMgr).
    //
    // The client already knows all the weather: when it loads, core::CWeatherMgr turns each weather
    // table in db/weathercommon.xml ("type1".."type12": seed, 53 weekly rows of clear/cloudy/rain/
    // thunderstorm weights) into a year of 20-minute slots. The page is built from those finished
    // slots, read from the client (pleione::CWorld -> CClientWeatherMgr -> core::CWeatherMgr), so it
    // shows exactly what the game will show. The server only tells the client which table the
    // region group it's in uses; the other regions come from a built-in list (the server's
    // weatherserver.xml), which assignments seen in game correct and extend.
    //
    // "You're here" is the region the game says the player is in (pleione::CWorld's current region,
    // and core::ITerrain for its group). Once the page has been made, Kanan keeps
    // MabiTrackersLive.js next to it up to date with that, so an open page follows the player.
    class MabiTrackers : public Mod {
    public:
        MabiTrackers();
        ~MabiTrackers();

        void onFrame() override;
        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

        // Writes the page to the game folder and opens it in the browser. On failure, why.
        bool open(std::wstring& error);

        // The weather where the player is and what comes next (for .weather).
        struct WeatherNow {
            std::wstring place;     // the region group's names, "" when it isn't a known one
            int now;                // 0 clear, 1 cloudy, 2 rain, 3 thunderstorm
            int next;
            uint64_t untilNext;     // milliseconds
            uint64_t nextLength;
            bool mayDiffer;         // a "may differ" slot is in the current or the next weather
        };

        // False with why when there's no weather here (such as Physis) or it isn't available.
        bool weatherHere(WeatherNow& out, std::wstring& error);

        static MabiTrackers* instance();

    private:
        // Region group -> weather table name, as seen in game.
        std::map<uint32_t, std::string> m_learned;
        uint32_t m_nextCheck;
        std::string m_status;

        // The live location for an open page (MabiTrackersLive.js): the region last written, when,
        // and whether the page has been made (so players who never open it get no file).
        uint32_t m_liveRegion;
        uint32_t m_liveWritten;
        int m_hasPage;

        void learnAssignments();
        void updateLive(uintptr_t mgr);
        std::string tableFor(uint32_t group) const;
    };
}
