#include <algorithm>
#include <cwctype>
#include <vector>

#include <imgui.h>

#include <Scan.hpp>

#include "Log.hpp"
#include "UserCommands.hpp"
#include "MabiTrackers.hpp"

using namespace std;

namespace kanan {
    // esl (ESL.dll)
    using StringCtorFn = void*(__thiscall*)(void* str, const wchar_t* text);
    using StringDefaultCtorFn = void*(__thiscall*)(void* str);
    using StringDtorFn = void(__thiscall*)(void* str);
    using StringContentFn = const wchar_t*(__thiscall*)(const void* str);
    // pleione::CInterfaceMgr's "add a line to the chat window" (not exported).
    using ShowChatLineFn = void(__thiscall*)(void* interfaceMgr, const void* name, const void* message, unsigned long unknown1, unsigned long type, const void* extra, unsigned long unknown2);

    static StringCtorFn g_stringCtor{ nullptr };
    static StringDefaultCtorFn g_stringDefaultCtor{ nullptr };
    static StringDtorFn g_stringDtor{ nullptr };
    static StringContentFn g_stringContent{ nullptr };
    static ShowChatLineFn g_showChatLine{ nullptr };
    static void** g_interfaceMgr{ nullptr };

    static UserCommands* g_userCommands{ nullptr };
    static function<int()> g_combatSwapQuery{};

    // core::stdapi_GetGlobalTime: the server's clock in milliseconds, as the client keeps it.
    using GetGlobalTimeFn = uint64_t(__cdecl*)();
    static GetGlobalTimeFn g_getGlobalTime{ nullptr };

    // An Erinn day is 36 real minutes (core::stdapi_GlobalTimeToGameDay divides by this).
    static const uint64_t ERINN_DAY_MS = 36 * 60 * 1000;

    // Price's stops, by Erinn day % 14 (GetTargetPosition in the server's npc/common.mint). He has
    // two spots in Dunbarton, Bangor and Emain Macha, named by where each is from the other.
    static const wchar_t* const PRICE_STOPS[14] = {
        L"Tir Chonaill",
        L"Dugald Aisle",
        L"Dunbarton (east)",
        L"Gairech",
        L"Bangor (south)",
        L"Sen Mag Plains",
        L"Emain Macha (north)",
        L"Ceo Island",
        L"Emain Macha (south)",
        L"Sen Mag Plains",
        L"Gairech",
        L"Bangor (north)",
        L"Dunbarton (west)",
        L"Dugald Aisle",
    };

    // An Erinn hour is 90 real seconds.
    static const uint64_t ERINN_HOUR_MS = 90 * 1000;

    static uint64_t erinnHour(uint64_t time) {
        return time % ERINN_DAY_MS / ERINN_HOUR_MS;
    }

    // Rua (the server's npc/emainmacha/rua.mint): on club days of a 43-day cycle (a day running 6:00
    // to 6:00) she goes to Bean Rua from 17:00 to 6:00; on other days she goes home. In a club day's
    // daytime she stays where she was. 1 at Bean Rua, 0 at home.
    static bool isRuaClubDay(int64_t day) {
        static const int CLUB_DAYS[] = { 0, 2, 3, 5, 6, 7, 15, 17, 20, 24, 28, 31, 35, 40 };
        auto inCycle = (int)(((day % 43) + 43) % 43);

        return find(begin(CLUB_DAYS), end(CLUB_DAYS), inCycle) != end(CLUB_DAYS);
    }

    static int ruaAt(uint64_t time) {
        auto hour = erinnHour(time);
        auto day = (int64_t)(time / ERINN_DAY_MS) - (hour < 6 ? 1 : 0);

        if (!isRuaClubDay(day)) {
            return 0;
        }

        return hour >= 17 || hour < 6 || isRuaClubDay(day - 1) ? 1 : 0;
    }

    // Fleta (npc/senmag/fleta.mint): out for a walk in Sen Mag Plains after meals, 9:00-11:00,
    // 15:00-17:00 and 19:00-21:00. 1 out, 0 not.
    static int fletaAt(uint64_t time) {
        auto hour = erinnHour(time);

        return hour == 9 || hour == 10 || hour == 15 || hour == 16 || hour == 19 || hour == 20 ? 1 : 0;
    }

    // Tarlach (npc/variable/tarlach.mint, tarlachbear.mint): a man from 18:00 to 6:00, a bear
    // otherwise. 1 a man, 0 a bear.
    static int tarlachAt(uint64_t time) {
        auto hour = erinnHour(time);

        return hour >= 18 || hour < 6 ? 1 : 0;
    }

    // When a schedule that changes on Erinn hours next changes after `now`, and how long what comes
    // next lasts.
    struct Change {
        uint64_t at;
        uint64_t length;
    };

    static Change nextChange(int (*stateAt)(uint64_t), uint64_t now) {
        auto current = stateAt(now);
        auto limit = now + 60 * ERINN_DAY_MS;
        auto at = (now / ERINN_HOUR_MS + 1) * ERINN_HOUR_MS;

        while (at < limit && stateAt(at) == current) {
            at += ERINN_HOUR_MS;
        }

        auto next = stateAt(at);
        auto end = at;

        while (end < limit && stateAt(end) == next) {
            end += ERINN_HOUR_MS;
        }

        return { at, end - at };
    }

    // "2h 5m", "36m" or "1m": rounded up to the minute.
    static wstring formatDuration(uint64_t ms) {
        auto minutes = (ms + 59999) / 60000;

        if (minutes < 60) {
            return to_wstring(minutes) + L"m";
        }

        return to_wstring(minutes / 60) + L"h " + to_wstring(minutes % 60) + L"m";
    }

    // Today's shadow missions, picked the way the server does (CTodayShadowMissionMgr and
    // db/todayshadowmission.xml; the Erinn Tracker page does the same, see MabiTrackers.html): the
    // day turns at 7:00 on the game's clock and counts from 1 January of year 1; today's division is
    // day % (the largest division) + 1, and the mission is drawn from that division's list, in the
    // file's order, with esl::CRandom seeded with the day. The names are the client's own.
    static const uint64_t SHADOW_MISSION_RESET_MS = 7 * 60 * 60 * 1000;
    static const uint64_t DAY_MS = 24 * 60 * 60 * 1000;

    struct ShadowMission {
        const wchar_t* name;
        int division;
    };

    static const ShadowMission TAILLTEANN_MISSIONS[] = {
        { L"Defeat Fomor Commander I", 1 }, { L"Rescue the Scout", 2 }, { L"Battle for Taillteann I", 1 },
        { L"Battle for Taillteann II", 2 }, { L"Dorren's Request", 1 }, { L"Taillteann Defensive Battle", 1 },
        { L"Defeat Fomor Commander II", 2 }, { L"Defeat the Shadow Wizard", 2 }, { L"Offering", 2 }, { L"Provocation", 1 },
    };

    static const ShadowMission TARA_MISSIONS[] = {
        { L"Shadow Cast City", 1 }, { L"Lingering Darkness", 2 }, { L"Enemy Behind", 2 }, { L"Their Method", 1 },
        { L"The Other Alchemists", 1 }, { L"Ghost of Partholon", 2 }, { L"Fomor Attack", 2 },
        { L"The Sulfur Spider inside Shadow Realm", 1 },
    };

    // esl::CRandom (ESL.dll): MT19937 with the game's own seeding. Only its first few numbers are
    // drawn here, before it would need to refill.
    class EslRandom {
    public:
        explicit EslRandom(uint32_t seed) {
            auto x = seed;

            for (auto& value : m_mt) {
                uint32_t y = x * 69069u;
                value = (x & 0xFFFF0000u) | (y >> 16);
                x = y * 69069u + 69070u;
            }

            for (size_t i = 0; i < N; ++i) {
                auto next = m_mt[(i + 1) % N];
                uint32_t y = (m_mt[i] & 0x80000000u) | (next & 0x7FFFFFFFu);
                m_mt[i] = m_mt[(i + 397) % N] ^ (y >> 1) ^ ((next & 1) ? 0x9908B0DFu : 0u);
            }
        }

        uint32_t next() {
            uint32_t y = m_mt[m_index++ % N];
            y ^= y >> 11;
            y ^= (y << 7) & 0x9D2C5680u;
            y ^= (y << 15) & 0xEFC60000u;
            y ^= y >> 18;
            return y;
        }

        // RandomU32_N: draws masked to the next power of two until one is under n.
        uint32_t below(uint32_t n) {
            if (n == 0) {
                return 0;
            }

            auto mask = n - 1;
            mask |= mask >> 1; mask |= mask >> 2; mask |= mask >> 4; mask |= mask >> 8; mask |= mask >> 16;

            for (;;) {
                auto value = next() & mask;

                if (value < n) {
                    return value;
                }
            }
        }

    private:
        static const size_t N = 624;
        uint32_t m_mt[N]{};
        size_t m_index{ 0 };
    };

    template <size_t count>
    static const wchar_t* shadowMissionOn(const ShadowMission (&missions)[count], uint32_t day) {
        auto divisions = 1;

        for (auto& mission : missions) {
            divisions = mission.division > divisions ? mission.division : divisions;
        }

        auto division = (int)(day % divisions) + 1;
        vector<const wchar_t*> today{};

        for (auto& mission : missions) {
            if (mission.division == division) {
                today.push_back(mission.name);
            }
        }

        EslRandom random{ day };
        return today.empty() ? L"" : today[random.below((uint32_t)today.size())];
    }

    // "Rua is at home for another 2h 17m, then at Bean Rua in Emain Macha for 20m."
    static wstring nowThen(const wstring& now, uint64_t remaining, const wstring& then, uint64_t length) {
        return now + L" for another " + formatDuration(remaining) + L", then " + then + L" for " + formatDuration(length) + L".";
    }

    // The start of the game's chat input function: push 0Ch / mov eax, <handler>.
    static uintptr_t g_chatHandler{ 0 };
    static uintptr_t g_chatReturn{ 0 };
    static bool g_chatHandled{ false };

    static bool __stdcall handleChatInput(const void* message) {
        if (g_userCommands == nullptr) {
            return false;
        }

        return g_userCommands->onChatInput(g_stringContent(message));
    }

    // bool ChatInput(esl::CStringT message): the message is passed by value and the function
    // destroys it. For a command, destroy it here and return true (handled, not sent) like the
    // game does for "/" commands; otherwise run the game's function.
    static __declspec(naked) void hookChatInput() {
        __asm {
            lea     eax, [esp + 4]
            pushad
            pushfd
            push    eax
            call    handleChatInput
            mov     g_chatHandled, al
            popfd
            popad
            cmp     byte ptr g_chatHandled, 0
            jne     handled

            push    0Ch
            mov     eax, g_chatHandler
            jmp     g_chatReturn

        handled:
            lea     ecx, [esp + 4]
            call    g_stringDtor
            mov     al, 1
            ret     4
        }
    }

    // The start of the chat send filter that runs before the chat input function: push 24h /
    // mov eax, <handler>.
    static uintptr_t g_filterHandler{ 0 };
    static uintptr_t g_filterReturn{ 0 };

    // bool SendFilter(esl::CStringT message, x, y, z): drops messages sent too quickly and repeats of
    // the last message ("skip repeated messages for network stability"), then sends the rest on to
    // the chat input function. Commands are answered here, before those checks, the same way as in
    // hookChatInput: destroy the message and return true.
    static __declspec(naked) void hookChatFilter() {
        __asm {
            lea     eax, [esp + 4]
            pushad
            pushfd
            push    eax
            call    handleChatInput
            mov     g_chatHandled, al
            popfd
            popad
            cmp     byte ptr g_chatHandled, 0
            jne     handled

            push    24h
            mov     eax, g_filterHandler
            jmp     g_filterReturn

        handled:
            lea     ecx, [esp + 4]
            call    g_stringDtor
            mov     al, 1
            ret     10h
        }
    }

    UserCommands::UserCommands()
        : PatchMod{ "Chat Commands", "Adds chat commands such as .help and .ping." },
        m_enabled{ false },
        m_isAvailable{ false },
        m_patch{},
        m_filterPatch{}
    {
        log("[UserCommands] Entering constructor...");

        auto esl = GetModuleHandleA("ESL.dll");
        auto pleione = GetModuleHandleA("Pleione.dll");

        if (esl == nullptr || pleione == nullptr) {
            log("[UserCommands] Game modules are not loaded.");
            log("[UserCommands] Leaving constructor.");
            return;
        }

        g_stringCtor = (StringCtorFn)GetProcAddress(esl, "??0?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QAE@PB_W@Z");
        g_stringDefaultCtor = (StringDefaultCtorFn)GetProcAddress(esl, "??0?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QAE@XZ");
        g_stringDtor = (StringDtorFn)GetProcAddress(esl, "??1?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QAE@XZ");
        g_stringContent = (StringContentFn)GetProcAddress(esl, "?GetSafeContent@?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QBEPB_WXZ");
        g_interfaceMgr = (void**)GetProcAddress(pleione, "?s_pInstanceBlock@?$TSingleton@VCInterfaceMgr@pleione@@@esl@@0PAEA");

        // Only needed by .price, which says so when it's missing.
        auto standard = GetModuleHandleA("Standard.dll");

        if (standard != nullptr) {
            g_getGlobalTime = (GetGlobalTimeFn)GetProcAddress(standard, "?stdapi_GetGlobalTime@core@@YA_KXZ");
        }

        // CInterfaceMgr's chat line function:
        //   push 0Ch / mov eax, <handler> / call <prolog> / mov edi, ecx / xor ebx, ebx /
        //   cmp [edi+48h], bl / jne ... / mov eax, [CWindowMgr instance] / cmp [eax+489h], bl
        auto showChatLine = scan("Pleione.dll", "6A 0C B8 ? ? ? ? E8 ? ? ? ? 8B F9 33 DB 38 5F 48 0F 85 ? ? ? ? A1 ? ? ? ? 38 98 89 04 00 00");

        // The chat input function:
        //   push 0Ch / mov eax, <handler> / call <prolog> / and [ebp-4], 0 / push 0 /
        //   lea ecx, [ebp+8] / nop / call CStringT::GetAt / mov esi, [CStringT::~CStringT] /
        //   cmp ax, '/' / je
        auto chatInput = scan("Pleione.dll", "6A 0C B8 ? ? ? ? E8 ? ? ? ? 83 65 FC 00 6A 00 8D 4D 08 90 E8 ? ? ? ? 8B 35 ? ? ? ? 66 83 F8 2F 74");

        if (g_stringCtor == nullptr || g_stringDefaultCtor == nullptr || g_stringDtor == nullptr ||
            g_stringContent == nullptr || g_interfaceMgr == nullptr || !showChatLine || !chatInput) {
            log("[UserCommands] Failed to find the chat functions.");
            log("[UserCommands] Leaving constructor.");
            return;
        }

        // The chat input function must destroy its message with the CStringT destructor.
        if (**(uintptr_t**)(*chatInput + 29) != (uintptr_t)g_stringDtor) {
            log("[UserCommands] Unexpected code at %p, not patching.", *chatInput);
            log("[UserCommands] Leaving constructor.");
            return;
        }

        g_showChatLine = (ShowChatLineFn)*showChatLine;
        g_chatHandler = *(uintptr_t*)(*chatInput + 3);
        g_chatReturn = *chatInput + 7;

        auto rel = (uintptr_t)&hookChatInput - (*chatInput + 5);

        m_patch.address = *chatInput;
        m_patch.bytes = { 0xE9, (int16_t)(rel & 0xFF), (int16_t)((rel >> 8) & 0xFF), (int16_t)((rel >> 16) & 0xFF), (int16_t)((rel >> 24) & 0xFF), 0x90, 0x90 };
        m_isAvailable = true;
        g_userCommands = this;

        // The chat send filter (repeat and flood checks), which passes messages on to the chat
        // input function. Commands are caught here too so those checks never see them; without it
        // they are still answered, but a repeated long command gets "skip repeated messages".
        //   push 24h / mov eax, <handler> / call <prolog> / mov edi, ecx / and [ebp-4], 0 / nop /
        //   call / push eax / lea eax, [ebp+8] / push eax / lea ecx, [ebp-14h] / call /
        //   mov byte ptr [ebp-4], 1
        auto chatFilter = scan("Pleione.dll", "6A 24 B8 ? ? ? ? E8 ? ? ? ? 8B F9 83 65 FC 00 90 E8 ? ? ? ? 50 8D 45 08 50 8D 4D EC E8 ? ? ? ? C6 45 FC 01");

        // It must also destroy its message with the CStringT destructor: mov esi, [~CStringT] at +57h.
        if (chatFilter && *(uint16_t*)(*chatFilter + 0x57) == 0x358B && **(uintptr_t**)(*chatFilter + 0x59) == (uintptr_t)g_stringDtor) {
            g_filterHandler = *(uintptr_t*)(*chatFilter + 3);
            g_filterReturn = *chatFilter + 7;

            auto filterRel = (uintptr_t)&hookChatFilter - (*chatFilter + 5);

            m_filterPatch.address = *chatFilter;
            m_filterPatch.bytes = { 0xE9, (int16_t)(filterRel & 0xFF), (int16_t)((filterRel >> 8) & 0xFF), (int16_t)((filterRel >> 16) & 0xFF), (int16_t)((filterRel >> 24) & 0xFF), 0x90, 0x90 };

            log("[UserCommands] Found the chat send filter at %p", *chatFilter);
        }
        else {
            log("[UserCommands] Failed to find the chat send filter; repeated commands may be skipped by the game.");
        }

        log("[UserCommands] Found the chat input at %p and the chat window at %p", *chatInput, *showChatLine);
        log("[UserCommands] Leaving constructor.");
    }

    UserCommands::~UserCommands() {
        if (g_userCommands == this) {
            g_userCommands = nullptr;
        }
    }

    void UserCommands::setCombatSwapQuery(function<int()> query) {
        g_combatSwapQuery = move(query);
    }

    void UserCommands::printToChat(const wstring& message) {
        auto interfaceMgr = *g_interfaceMgr;

        if (interfaceMgr == nullptr) {
            return;
        }

        // esl::CStringT is a single pointer.
        uintptr_t name[4]{};
        uintptr_t text[4]{};
        uintptr_t extra[4]{};

        g_stringCtor(name, L"<Kanan>");
        g_stringCtor(text, message.c_str());
        g_stringDefaultCtor(extra);
        g_showChatLine(interfaceMgr, name, text, 0, 0, extra, 0);
        g_stringDtor(extra);
        g_stringDtor(text);
        g_stringDtor(name);
    }

    bool UserCommands::onChatInput(const wchar_t* message) {
        if (!m_enabled || message == nullptr || message[0] != L'.') {
            return false;
        }

        // The command is the word after the dot; anything else is a normal chat message.
        wstring text{ message + 1 };
        auto end = text.find(L' ');
        auto command = text.substr(0, end);

        if (command.empty() || !all_of(command.begin(), command.end(), [](wchar_t c) { return iswalpha(c) != 0; })) {
            return false;
        }

        transform(command.begin(), command.end(), command.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });

        if (command == L"help" || command == L"h") {
            printToChat(
                L"Available commands:\n"
                L".help .h - shows the available commands\n"
                L".ping .p - answers 'pong'\n"
                L".swap .s - tells which skill the combat attack is swapped to\n"
                L".price .rua .fleta .tarlach - where they are and how long until that changes\n"
                L".sm - today's shadow missions in Taillteann and Tara\n"
                L".weather - the weather where you are and what comes next\n"
                L".tracker - opens the Erinn Tracker in your browser: the weather everywhere, where the moon gates lead, and when Price, Rua, Fleta and Tarlach are where"
            );
        }
        else if (command == L"tracker") {
            auto tracker = MabiTrackers::instance();
            wstring error;

            if (tracker == nullptr) {
                printToChat(L"The Erinn Tracker is not available.");
            }
            else if (tracker->open(error)) {
                printToChat(L"Opened the Erinn Tracker in your browser.");
            }
            else {
                printToChat(error);
            }
        }
        else if (command == L"price") {
            if (g_getGlobalTime == nullptr) {
                printToChat(L"Price's location is not available for this version of the game.");
                return true;
            }

            auto now = g_getGlobalTime();
            auto day = now / ERINN_DAY_MS;
            auto intoDay = now % ERINN_DAY_MS;
            auto untilNextDay = ERINN_DAY_MS - intoDay;
            auto stop = [&](uint64_t days) { return PRICE_STOPS[(day + days) % 14]; };

            // He checks where to be every 30-40 seconds, so he may still be on his way.
            auto here = L"Price is in " + wstring{ stop(0) } + (intoDay < 40 * 1000 ? L" (arriving within a minute)" : L"");

            printToChat(nowThen(here, untilNextDay, L"in " + wstring{ stop(1) }, ERINN_DAY_MS));
        }
        else if (command == L"weather") {
            auto tracker = MabiTrackers::instance();
            MabiTrackers::WeatherNow weather{};
            wstring error;

            if (tracker == nullptr) {
                printToChat(L"The weather is not available.");
            }
            else if (!tracker->weatherHere(weather, error)) {
                printToChat(error);
            }
            else {
                static const wchar_t* const NOW[] = { L"It's clear", L"It's cloudy", L"It's raining", L"There's a thunderstorm" };
                static const wchar_t* const THEN[] = { L"clear", L"cloudy", L"rain", L"a thunderstorm" };
                auto where = weather.place.empty() ? wstring{ L" here" } : L" in " + weather.place;

                printToChat(nowThen(NOW[weather.now] + where, weather.untilNext, THEN[weather.next], weather.nextLength) +
                    (weather.mayDiffer ? L" Part of this may differ for other players and the game servers (see .tracker)." : L""));
            }
        }
        else if (command == L"sm") {
            if (g_getGlobalTime == nullptr) {
                printToChat(L"Today's shadow missions are not available for this version of the game.");
                return true;
            }

            auto now = g_getGlobalTime();
            auto day = (uint32_t)((now - SHADOW_MISSION_RESET_MS) / DAY_MS + 1);
            auto untilNext = (uint64_t)day * DAY_MS + SHADOW_MISSION_RESET_MS - now;

            printToChat(L"Today's shadow missions are " + wstring{ shadowMissionOn(TAILLTEANN_MISSIONS, day) } +
                L" in Taillteann and " + shadowMissionOn(TARA_MISSIONS, day) + L" in Tara, for another " +
                formatDuration(untilNext) + L" (they change at 7:00 AM server time).");
        }
        else if (command == L"rua" || command == L"fleta" || command == L"tarlach") {
            if (g_getGlobalTime == nullptr) {
                printToChat(L"NPC schedules are not available for this version of the game.");
                return true;
            }

            auto now = g_getGlobalTime();

            // Each schedule has two states (1 and 0): how to say each one now, and after "then".
            struct Schedule {
                int (*stateAt)(uint64_t);
                const wchar_t* now[2];
                const wchar_t* then[2];
            };

            static const Schedule RUA{ ruaAt,
                { L"Rua is at home", L"Rua is at Bean Rua in Emain Macha" },
                { L"at home", L"at Bean Rua in Emain Macha" } };
            static const Schedule FLETA{ fletaAt,
                { L"Fleta is away", L"Fleta is out for a walk in Sen Mag Plains" },
                { L"away", L"out for a walk in Sen Mag Plains" } };
            static const Schedule TARLACH{ tarlachAt,
                { L"Tarlach is a bear in North Sidhe Sneachta", L"Tarlach is a man in North Sidhe Sneachta" },
                { L"a bear", L"a man" } };

            auto& schedule = command == L"rua" ? RUA : command == L"fleta" ? FLETA : TARLACH;
            auto state = schedule.stateAt(now);
            auto next = nextChange(schedule.stateAt, now);

            printToChat(nowThen(schedule.now[state], next.at - now, schedule.then[1 - state], next.length));
        }
        else if (command == L"ping" || command == L"p") {
            printToChat(L"pong");
        }
        else if (command == L"swap" || command == L"s") {
            if (!g_combatSwapQuery) {
                printToChat(L"Combat attack swap is not available");
            }
            else if (auto skillID = g_combatSwapQuery(); skillID != 0) {
                printToChat(L"Combat attack currently swapped to Skill ID: " + to_wstring(skillID));
            }
            else {
                printToChat(L"Combat attack is not swapped currently");
            }
        }
        else if (command == L"reload" || command == L"r") {
            printToChat(L"Kanan applies setting changes right away. Open the Kanan window to change them.");
        }
        else {
            printToChat(L"Invalid command. Type .help for the available commands.");
        }

        return true;
    }

    void UserCommands::apply() {
        if (!m_isAvailable) {
            return;
        }

        log("[UserCommands] Toggling %s", m_enabled ? "on" : "off");

        if (m_enabled) {
            patch(m_patch);

            if (m_filterPatch.address != 0) {
                patch(m_filterPatch);
            }
        }
        else {
            if (m_filterPatch.address != 0) {
                undoPatch(m_filterPatch);
            }

            undoPatch(m_patch);
        }
    }

    void UserCommands::onPatchUI() {
        if (!m_isAvailable) {
            return;
        }

        if (ImGui::Checkbox("Chat Commands", &m_enabled)) {
            apply();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Type these in chat (they are not sent to other players):\n.help - list of commands\n.ping - answers pong\n.swap - which skill the combat attack is swapped to\n"
                ".price .rua .fleta .tarlach - where they are and how long until that changes\n"
                ".sm - today's shadow missions in Taillteann and Tara\n"
                ".weather - the weather where you are and what comes next\n"
                ".tracker - opens the Erinn Tracker: the weather everywhere, moon gates, and NPC schedules");
        }
    }

    void UserCommands::onConfigLoad(const Config& cfg) {
        m_enabled = cfg.get<bool>("UserCommands.Enabled").value_or(false);

        if (m_enabled) {
            apply();
        }
    }

    void UserCommands::onConfigSave(Config& cfg) {
        cfg.set<bool>("UserCommands.Enabled", m_enabled);
    }
}
