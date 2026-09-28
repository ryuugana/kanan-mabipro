#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <set>
#include <sstream>
#include <vector>

#include <Windows.h>
#include <shellapi.h>

#include <imgui.h>
#include <json.hpp>
#include <String.hpp>

#include "Log.hpp"
#include "WeatherTracker.hpp"

#pragma comment(lib, "shell32.lib")

using namespace std;
using nlohmann::json;

namespace kanan {
    static WeatherTracker* g_weatherTracker{ nullptr };

    // esl::CStringT<wchar_t> functions (ESL.dll). A CStringT is a single pointer.
    using StringCtorFn = void*(__thiscall*)(void* str, const wchar_t* text);
    using StringDtorFn = void(__thiscall*)(void* str);
    using StringContentFn = const wchar_t*(__thiscall*)(const void* str);
    // core::CWeatherMgr::FindWeatherTable(const CStringT& name) -> SWeatherTable*
    using FindTableFn = const uint8_t*(__thiscall*)(void* mgr, const void* name);
    using GetGlobalTimeFn = uint64_t(__cdecl*)();
    // core::ITerrain::GetRegionGroupID(region id): the region's group, 0 for an unknown region.
    using GetRegionGroupIdFn = uint32_t(__thiscall*)(void* terrain, uint32_t region);

    static bool g_triedExports{ false };
    static uintptr_t* g_worldBlock{ nullptr };      // TSingleton<pleione::CWorld>::s_pInstanceBlock
    static void* g_weatherMgrVtable{ nullptr };     // core::CWeatherMgr's vtable
    static FindTableFn g_findTable{ nullptr };
    static GetGlobalTimeFn g_getGlobalTime{ nullptr };
    static GetRegionGroupIdFn g_getRegionGroupId{ nullptr };
    static StringCtorFn g_stringCtor{ nullptr };
    static StringDtorFn g_stringDtor{ nullptr };
    static StringContentFn g_stringContent{ nullptr };

    // Region groups with weather and their weather tables, from the server's weatherserver.xml. The
    // names are the game's own for the regions in each group (MabiPro's region files and minimap
    // names, db/minimapinfo.xml); minor places are shown on a card's "also" line.
    struct KnownGroup {
        uint32_t group;
        const char* table;
        const char* names;      // separated by '|'
        bool minor;
    };

    static const KnownGroup KNOWN_GROUPS[] = {
        { 1, "type1", "Tir Chonaill", false },
        { 50, "type1", "Dugald Aisle", false },
        { 2000, "type1", "Dugald Aisle Residential Area", true },
        { 2001, "type1", "Dugald Aisle Castle", true },
        { 100, "type2", "Dunbarton", false },
        { 150, "type2", "Gairech", false },
        { 10400, "type2", "Fiodh Dungeon Lobby", true },
        { 200, "type3", "Bangor", false },
        { 400, "type4", "Emain Macha", false },
        { 450, "type5", "Sen Mag Plains", false },
        { 2002, "type5", "Sen Mag Residential Area", true },
        { 2003, "type5", "Sen Mag Castle", true },
        { 64, "type6", "Morva Aisle", false },
        { 65, "type6", "Port Ceann", false },
        { 2, "type7", "Rano|Port Qilla", false },
        { 4, "type8", "Connous", false },
        { 21, "type9", "Courcle", false },
        { 23, "type10", "Zardine", false },
        { 28, "type12", "Taillteann", false },
        { 29, "type12", "Sliab Cuilin|Abb Neagh", false },
        { 31, "type12", "Corrib Valley|Blago Prairie", false },
        { 32, "type12", "Tara", false },
    };

    static vector<string> splitNames(const char* names) {
        vector<string> out;
        stringstream ss{ names };
        string name;

        while (getline(ss, name, '|')) {
            out.push_back(name);
        }

        return out;
    }

    // How far the page looks back and ahead, in 20-minute slots.
    static const int SLOTS_BEFORE = 3 * 24;
    static const int SLOTS_AFTER = 14 * 24 * 3;

    // The page, built into Kanan.asi from WeatherTracker.html (WeatherTracker.rc).
    static const char* PAGE_PLACEHOLDER = "/*KANAN_WEATHER_DATA*/null";

    static void resolveExports() {
        if (g_triedExports) {
            return;
        }

        g_triedExports = true;

        auto pleione = GetModuleHandleA("Pleione.dll");
        auto standard = GetModuleHandleA("Standard.dll");
        auto esl = GetModuleHandleA("ESL.dll");

        if (pleione == nullptr || standard == nullptr || esl == nullptr) {
            return;
        }

        g_worldBlock = (uintptr_t*)GetProcAddress(pleione, "?s_pInstanceBlock@?$TSingleton@VCWorld@pleione@@@esl@@0PAEA");
        g_weatherMgrVtable = (void*)GetProcAddress(standard, "??_7CWeatherMgr@core@@6B@");
        g_findTable = (FindTableFn)GetProcAddress(standard,
            "?FindWeatherTable@CWeatherMgr@core@@QAEPBUSWeatherTable@2@ABV?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@@Z");
        g_getGlobalTime = (GetGlobalTimeFn)GetProcAddress(standard, "?stdapi_GetGlobalTime@core@@YA_KXZ");
        g_getRegionGroupId = (GetRegionGroupIdFn)GetProcAddress(standard, "?GetRegionGroupID@ITerrain@core@@QBEKK@Z");
        g_stringCtor = (StringCtorFn)GetProcAddress(esl, "??0?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QAE@PB_W@Z");
        g_stringDtor = (StringDtorFn)GetProcAddress(esl, "??1?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QAE@XZ");
        g_stringContent = (StringContentFn)GetProcAddress(esl, "?GetSafeContent@?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QBEPB_WXZ");

        log("[WeatherTracker] World %p, weather manager vtable %p, FindWeatherTable %p, GetGlobalTime %p",
            g_worldBlock, g_weatherMgrVtable, g_findTable, g_getGlobalTime);
    }

    static bool hasExports() {
        resolveExports();

        return g_worldBlock != nullptr && g_weatherMgrVtable != nullptr && g_findTable != nullptr &&
            g_getGlobalTime != nullptr && g_stringCtor != nullptr && g_stringDtor != nullptr && g_stringContent != nullptr;
    }

    // pleione::CWorld -> +20h pleione::CClientWeatherMgr -> +4 -> +4 core::CWeatherMgr, checked by
    // its vtable. 0 when the game hasn't set it up (or the layout isn't the expected one).
    static uintptr_t findWeatherManagerUnsafe() {
        auto world = *g_worldBlock;
        auto clientWeather = world ? *(uintptr_t*)(world + 0x20) : 0;
        auto inner = clientWeather ? *(uintptr_t*)(clientWeather + 4) : 0;
        auto mgr = inner ? inner + 4 : 0;

        return (mgr != 0 && *(void**)mgr == g_weatherMgrVtable) ? mgr : 0;
    }

    static uintptr_t findWeatherManager() {
        if (!hasExports()) {
            return 0;
        }

        __try {
            return findWeatherManagerUnsafe();
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    // An RTTI class name ("?AVCWeatherDescTable@@"), or "" when there is none.
    static const char* className(uintptr_t object) {
        auto vtable = *(uintptr_t*)object;
        auto locator = vtable ? *(uintptr_t*)(vtable - 4) : 0;
        auto typeDescriptor = locator ? *(uintptr_t*)(locator + 12) : 0;

        return typeDescriptor ? (const char*)(typeDescriptor + 8) : "";
    }

    // The server's weather for a region group: which weather table, or "-" for other weather
    // (constant weather, no table).
    struct Assignment {
        uint32_t group;
        char table[32];
    };

    // core::CWeatherMgr's region group map (STLport map<unsigned long, channels*>; nodes: color,
    // parent, left, right, key +10h, value +14h). The value points at one 16-byte entry per weather
    // channel ({IWeatherDesc*, ?, start time}); channel 0 is what the server sent.
    static void collectGroups(uintptr_t node, Assignment* out, int& count, int max, int depth) {
        if (node == 0 || count >= max || depth > 64) {
            return;
        }

        collectGroups(*(uintptr_t*)(node + 8), out, count, max, depth + 1);

        auto value = *(uintptr_t*)(node + 0x14);
        auto entries = value ? *(uintptr_t*)value : 0;
        auto desc = entries ? *(uintptr_t*)entries : 0;

        if (desc != 0 && count < max) {
            auto& a = out[count++];

            a.group = *(uint32_t*)(node + 0x10);
            strcpy_s(a.table, "-");

            if (strstr(className(desc), "CWeatherDescTable") != nullptr) {
                auto name = g_stringContent((const void*)(desc + 8));

                if (name != nullptr) {
                    sprintf_s(a.table, "%S", name);
                }
            }
        }

        collectGroups(*(uintptr_t*)(node + 12), out, count, max, depth + 1);
    }

    static int readAssignments(uintptr_t mgr, Assignment* out, int max) {
        int count = 0;

        __try {
            auto impl = *(uintptr_t*)(mgr + 4);

            if (impl != 0) {
                collectGroups(*(uintptr_t*)(impl + 0x24 + 4), out, count, max, 0);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            log("[WeatherTracker] Reading the weather assignments failed");
        }

        return count;
    }

    // The region group a region belongs to, from the terrain the weather manager was started with
    // (its implementation keeps the ITerrain* at +8). 0 when it isn't known.
    static uint32_t regionGroupOf(uintptr_t mgr, uint32_t region) {
        if (g_getRegionGroupId == nullptr) {
            return 0;
        }

        __try {
            auto impl = *(uintptr_t*)(mgr + 4);
            auto terrain = impl ? *(void**)(impl + 8) : nullptr;

            return terrain ? g_getRegionGroupId(terrain, region) : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    // The region the player is in now, read the way the game's own current-region getter on
    // pleione::CWorld does (it's what the game asks the weather for): [[[world + 0Ch] + 15Ch] + 6Ch].
    // (The region in the character's parameters is where they logged in, and doesn't follow them.)
    // 0 when it isn't available.
    static uint32_t currentRegion() {
        __try {
            auto world = *g_worldBlock;
            auto a = world ? *(uintptr_t*)(world + 0x0C) : 0;
            auto b = a ? *(uintptr_t*)(a + 0x15C) : 0;

            return b ? *(uint32_t*)(b + 0x6C) : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    // core::SWeatherTable: slot length (ms, +0), start (+8), vector<float> of slot weather (+10h).
    struct WeatherTable {
        uint64_t slot;
        uint64_t base;
        const float* values;
        uint32_t count;
    };

    static bool copyTable(const uint8_t* table, WeatherTable& out) {
        __try {
            out.slot = *(const uint64_t*)table;
            out.base = *(const uint64_t*)(table + 8);
            out.values = *(const float* const*)(table + 0x10);
            out.count = (uint32_t)(*(const float* const*)(table + 0x14) - out.values);
            return out.slot != 0 && out.values != nullptr && out.count != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool findTable(uintptr_t mgr, const string& name, WeatherTable& out) {
        uintptr_t text[4]{};
        auto wide = widen(name);

        g_stringCtor(text, wide.c_str());
        auto table = g_findTable((void*)mgr, text);
        g_stringDtor(text);

        return table != nullptr && copyTable(table, out);
    }

    // Whether a table entry is one of the game's "session" entries. The table is made with
    // esl::CRandom, one draw per entry, and ESL's MT19937 counts down 624 but refills only after
    // the count passes 0: after the first refill each batch gives a 625th value, read from past
    // its 624 numbers (the generator's own position pointer, a memory address). So entry 1248 and
    // every 625th after it depend on where the game's memory landed: the same all game session,
    // but different after restarting the game, and for other players and the servers.
    static bool isSessionEntry(uint64_t entry) {
        return entry >= 1248 && (entry - 1248) % 625 == 0;
    }

    // IWeatherDesc::GetWeatherType: 0 clear, 1 cloudy, 2 rain, 3 thunderstorm.
    static char weatherType(float value) {
        if (value == 2.0f) {
            return '3';
        }

        if (value >= 1.9499f) {
            return '2';
        }

        return value >= 1.0f ? '1' : '0';
    }

    static uint64_t unixMilliseconds() {
        FILETIME ft{};
        GetSystemTimeAsFileTime(&ft);

        return ((((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime) - 116444736000000000ull) / 10000;
    }

    static string loadPage() {
        HMODULE module{};

        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)&loadPage, &module)) {
            return "";
        }

        auto resource = FindResourceW(module, L"WEATHER_TRACKER_HTML", RT_RCDATA);
        auto data = resource ? LoadResource(module, resource) : nullptr;
        auto bytes = data ? (const char*)LockResource(data) : nullptr;

        return bytes ? string{ bytes, SizeofResource(module, resource) } : "";
    }

    // The game folder, where the page and its live file go.
    static wstring gameFolder() {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        wstring path{ exe };

        return path.substr(0, path.find_last_of(L'\\') + 1);
    }

    WeatherTracker::WeatherTracker()
        : m_learned{},
        m_nextCheck{ 0 },
        m_status{},
        m_liveRegion{ 0 },
        m_liveWritten{ 0 },
        m_hasPage{ -1 }
    {
        g_weatherTracker = this;
    }

    WeatherTracker::~WeatherTracker() {
        if (g_weatherTracker == this) {
            g_weatherTracker = nullptr;
        }
    }

    WeatherTracker* WeatherTracker::instance() {
        return g_weatherTracker;
    }

    // Every few seconds: remember the server's weather for the region group the player is in, so
    // the page follows MabiPro if it assigns weather differently from the built-in list.
    void WeatherTracker::onFrame() {
        auto now = GetTickCount();

        if ((int32_t)(now - m_nextCheck) < 0) {
            return;
        }

        m_nextCheck = now + 5000;
        learnAssignments();

        if (auto mgr = findWeatherManager()) {
            updateLive(mgr);
        }
    }

    // The weather table a region group uses: what the server sent, or the built-in list. "" when it
    // has none (constant weather).
    string WeatherTracker::tableFor(uint32_t group) const {
        auto learned = m_learned.find(group);

        if (learned != m_learned.end()) {
            return learned->second == "-" ? "" : learned->second;
        }

        for (auto& g : KNOWN_GROUPS) {
            if (g.group == group) {
                return g.table;
            }
        }

        return "";
    }

    // Keeps kananWeatherLive.js, next to the page, up to date with where the player is, so an open
    // page can move its "You're here" badge. Written when the region changes, and once a minute so
    // the page can tell the game is still running.
    void WeatherTracker::updateLive(uintptr_t mgr) {
        if (m_hasPage < 0) {
            m_hasPage = GetFileAttributesW((gameFolder() + L"kananWeather.html").c_str()) != INVALID_FILE_ATTRIBUTES;
        }

        auto region = currentRegion();
        auto now = GetTickCount();

        if (m_hasPage == 0 || region == 0 || (region == m_liveRegion && now - m_liveWritten < 60000)) {
            return;
        }

        auto group = regionGroupOf(mgr, region);
        json live{ { "region", region }, { "group", group }, { "table", tableFor(group) }, { "time", unixMilliseconds() } };
        auto text = "kananLive(" + live.dump() + ");\n";

        FILE* f{};

        if (_wfopen_s(&f, (gameFolder() + L"kananWeatherLive.js").c_str(), L"wb") == 0 && f != nullptr) {
            fwrite(text.data(), 1, text.size(), f);
            fclose(f);

            if (region != m_liveRegion) {
                log("[WeatherTracker] Live location: region %u, region group %u", region, group);
            }

            m_liveRegion = region;
            m_liveWritten = now;
        }
    }

    void WeatherTracker::learnAssignments() {
        auto mgr = findWeatherManager();

        if (mgr == 0) {
            return;
        }

        Assignment assignments[64]{};
        auto count = readAssignments(mgr, assignments, 64);

        for (int i = 0; i < count; ++i) {
            auto& a = assignments[i];
            auto known = m_learned.find(a.group);

            if (known == m_learned.end() || known->second != a.table) {
                log("[WeatherTracker] Region group %u uses weather %s", a.group, a.table);
                m_learned[a.group] = a.table;
            }
        }
    }

    bool WeatherTracker::open(wstring& error) {
        auto mgr = findWeatherManager();

        if (mgr == 0) {
            error = L"The game's weather isn't available yet. Try again once you're in the game.";
            return false;
        }

        learnAssignments();

        // Which table each region group uses: the built-in list, corrected by what the server sent.
        map<uint32_t, string> groupTable;

        for (auto& g : KNOWN_GROUPS) {
            groupTable[g.group] = g.table;
        }

        for (auto& [group, table] : m_learned) {
            groupTable[group] = table;
        }

        // Where the player is: their character's region, and the game's region group for it. (The
        // weather manager keeps every group the server has sent weather for, not just this one.)
        set<uint32_t> hereGroups;
        auto region = currentRegion();
        auto group = region ? regionGroupOf(mgr, region) : 0;

        log("[WeatherTracker] You're in region %u, region group %u", region, group);

        if (group != 0) {
            hereGroups.insert(group);
        }

        auto now = g_getGlobalTime();
        auto unixNow = unixMilliseconds();

        json data;
        data["version"] = 1;
        data["generated"] = unixNow;
        data["offset"] = (int64_t)(now - unixNow);

        json tables = json::object();
        json places = json::array();
        set<string> tableNames;

        for (auto& [group, table] : groupTable) {
            if (table != "-") {
                tableNames.insert(table);
            }
        }

        for (auto& name : tableNames) {
            WeatherTable table{};

            if (!findTable(mgr, name, table)) {
                log("[WeatherTracker] The game has no weather table %s", name.c_str());
                continue;
            }

            // Slot k (starting base + k * slot) shows values[k + 1]; the first minute of each slot
            // fades in from the previous weather.
            auto current = (int64_t)((now - table.base) / table.slot);
            auto first = current - SLOTS_BEFORE;
            string weather;
            json unsure = json::array();

            weather.reserve(SLOTS_BEFORE + SLOTS_AFTER);

            for (int64_t k = first; k < current + SLOTS_AFTER; ++k) {
                auto entry = (uint64_t)(k + 1) % table.count;

                weather += weatherType(table.values[entry]);

                if (isSessionEntry(entry)) {
                    unsure.push_back(k - first);
                }
            }

            tables[name] = { { "base", table.base }, { "slot", table.slot }, { "first", first }, { "weather", weather }, { "unsure", unsure } };
        }

        // One place per table, named after its region groups, in the built-in order.
        vector<string> order;

        for (auto& g : KNOWN_GROUPS) {
            auto table = groupTable[g.group];

            if (table != "-" && find(order.begin(), order.end(), table) == order.end()) {
                order.push_back(table);
            }
        }

        for (auto& [group, table] : groupTable) {
            if (table != "-" && find(order.begin(), order.end(), table) == order.end()) {
                order.push_back(table);
            }
        }

        for (auto& table : order) {
            if (tables.find(table) == tables.end()) {
                continue;
            }

            vector<string> names, also;
            bool isHere = false;

            for (auto& g : KNOWN_GROUPS) {
                if (groupTable[g.group] != table) {
                    continue;
                }

                for (auto& name : splitNames(g.names)) {
                    auto& list = g.minor ? also : names;

                    if (find(list.begin(), list.end(), name) == list.end()) {
                        list.push_back(name);
                    }
                }
            }

            // Region groups the server gave weather that the built-in list doesn't know.
            for (auto& [group, groupTableName] : groupTable) {
                auto known = find_if(begin(KNOWN_GROUPS), end(KNOWN_GROUPS), [&](const KnownGroup& g) { return g.group == group; });

                if (groupTableName == table && known == end(KNOWN_GROUPS)) {
                    names.push_back("Region group " + to_string(group));
                }
            }

            for (auto group : hereGroups) {
                isHere |= groupTable[group] == table;
            }

            if (names.empty()) {
                names = also;
                also.clear();
            }

            places.push_back({ { "table", table }, { "names", names }, { "also", also }, { "here", isHere } });
        }

        data["tables"] = tables;
        data["places"] = places;

        auto page = loadPage();
        auto at = page.find(PAGE_PLACEHOLDER);

        if (at == string::npos) {
            error = L"Kanan's weather page is missing.";
            return false;
        }

        page.replace(at, strlen(PAGE_PLACEHOLDER), data.dump());

        auto path = gameFolder() + L"kananWeather.html";

        FILE* f{};

        if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || f == nullptr) {
            error = L"Can't write " + path;
            return false;
        }

        fwrite(page.data(), 1, page.size(), f);
        fclose(f);

        // The page follows the player from now on: write where they are right away.
        m_hasPage = 1;
        m_liveRegion = 0;
        updateLive(mgr);

        // Through Explorer, so the browser doesn't start with the game's administrator rights.
        auto quoted = L"\"" + path + L"\"";
        auto result = (intptr_t)ShellExecuteW(nullptr, L"open", L"explorer.exe", quoted.c_str(), nullptr, SW_SHOWNORMAL);

        log("[WeatherTracker] Wrote %S (%zu tables, %zu places), opening it: %d", path.c_str(), tables.size(), places.size(), (int)result);

        if (result <= 32) {
            error = L"Couldn't open " + path;
            return false;
        }

        return true;
    }

    void WeatherTracker::onUI() {
        if (ImGui::TreeNode("Weather Tracker")) {
            ImGui::TextWrapped(
                "A forecast of the weather in every region, with countdowns to clear skies, clouds, rain and thunderstorms. "
                "It opens in your browser; you can also type .weather in chat (with Chat Commands on). "
                "Leave it open while you play and it shows where you are."
            );
            ImGui::Spacing();

            if (ImGui::Button("Open Weather Forecast")) {
                wstring error;
                m_status = open(error) ? "Opened in your browser." : narrow(error);
            }

            if (!m_status.empty()) {
                ImGui::TextWrapped("%s", m_status.c_str());
            }

            ImGui::TreePop();
        }
    }

    // Learned assignments as "group:table,group:table".
    void WeatherTracker::onConfigLoad(const Config& cfg) {
        m_learned.clear();

        stringstream ss{ cfg.get("WeatherTracker.Groups").value_or("") };
        string item;

        while (getline(ss, item, ',')) {
            auto colon = item.find(':');

            if (colon == string::npos) {
                continue;
            }

            try {
                m_learned[(uint32_t)stoul(item.substr(0, colon))] = item.substr(colon + 1);
            }
            catch (...) {
            }
        }
    }

    void WeatherTracker::onConfigSave(Config& cfg) {
        string text;

        for (auto& [group, table] : m_learned) {
            if (!text.empty()) {
                text += ',';
            }

            text += to_string(group) + ':' + table;
        }

        cfg.set("WeatherTracker.Groups", text);
    }
}
