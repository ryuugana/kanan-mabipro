#include <algorithm>
#include <cwctype>

#include <imgui.h>

#include <Scan.hpp>

#include "Log.hpp"
#include "UserCommands.hpp"

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

    // Price's stops, by Erinn day % 14 (GetTargetPosition in the server's npc/common.mint).
    static const wchar_t* const PRICE_STOPS[14] = {
        L"Tir Chonaill",
        L"Dugald Aisle",
        L"Dunbarton",
        L"Gairech",
        L"Bangor",
        L"Sen Mag",
        L"Emain Macha",
        L"Ceo Island",
        L"Emain Macha (another spot)",
        L"Sen Mag",
        L"Gairech",
        L"Bangor (another spot)",
        L"Dunbarton (another spot)",
        L"Dugald Aisle",
    };

    // "2h 5m", "36m" or "1m": rounded up to the minute.
    static wstring formatDuration(uint64_t ms) {
        auto minutes = (ms + 59999) / 60000;

        if (minutes < 60) {
            return to_wstring(minutes) + L"m";
        }

        return to_wstring(minutes / 60) + L"h " + to_wstring(minutes % 60) + L"m";
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

        // Only needed by .price and .priceschedule, which say so when it's missing.
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
                L".price - where Price is and how long until he moves\n"
                L".priceschedule - how long until Price arrives at each of his next stops"
            );
        }
        else if (command == L"price" || command == L"priceschedule") {
            if (g_getGlobalTime == nullptr) {
                printToChat(L"Price's location is not available for this version of the game.");
                return true;
            }

            auto now = g_getGlobalTime();
            auto day = now / ERINN_DAY_MS;
            auto intoDay = now % ERINN_DAY_MS;
            auto untilNextDay = ERINN_DAY_MS - intoDay;
            auto stop = [&](uint64_t days) { return PRICE_STOPS[(day + days) % 14]; };

            if (command == L"price") {
                // He checks where to be every 30-40 seconds, so he may still be on his way.
                if (intoDay < 40 * 1000) {
                    printToChat(L"Price is moving to " + wstring{ stop(0) } + L" now (he arrives within a minute). " +
                        L"He moves to " + stop(1) + L" in " + formatDuration(untilNextDay) + L".");
                }
                else {
                    printToChat(L"Price is in " + wstring{ stop(0) } + L". He moves to " + stop(1) + L" in " +
                        formatDuration(untilNextDay) + L".");
                }
            }
            else {
                wstring schedule = L"Price is in " + wstring{ stop(0) } + L" for another " + formatDuration(untilNextDay) + L". Next:";

                for (uint64_t days = 1; days < 14; ++days) {
                    schedule += L"\n" + wstring{ stop(days) } + L" in " + formatDuration(untilNextDay + (days - 1) * ERINN_DAY_MS);
                }

                printToChat(schedule);
            }
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
                ".price - where Price is and how long until he moves\n.priceschedule - how long until Price arrives at each of his next stops");
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
