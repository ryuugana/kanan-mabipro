#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "Mod.hpp"

namespace kanan {
    // Weather Tracker: a forecast page for every region with weather, opened with .weather or from
    // the Kanan menu.
    //
    // The client already knows all the weather: when it loads, core::CWeatherMgr turns each weather
    // table in db/weathercommon.xml ("type1".."type12": seed, 53 weekly rows of clear/cloudy/rain/
    // thunderstorm weights) into a year of 20-minute slots. The page is built from those finished
    // slots, read from the client (pleione::CWorld -> CClientWeatherMgr -> core::CWeatherMgr), so it
    // shows exactly what the game will show. The server only tells the client which table the
    // region group it's in uses; the other regions come from a built-in list (the server's
    // weatherserver.xml), which assignments seen in game correct and extend.
    class WeatherTracker : public Mod {
    public:
        WeatherTracker();
        ~WeatherTracker();

        void onFrame() override;
        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

        // Writes the page to the game folder and opens it in the browser. On failure, why.
        bool open(std::wstring& error);

        static WeatherTracker* instance();

    private:
        // Region group -> weather table name, as seen in game.
        std::map<uint32_t, std::string> m_learned;
        uint32_t m_nextCheck;
        std::string m_status;

        void learnAssignments();
    };
}
