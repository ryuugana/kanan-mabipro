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

    UserCommands::UserCommands()
        : PatchMod{ "Chat Commands", "Adds chat commands such as .help and .ping." },
        m_enabled{ false },
        m_isAvailable{ false },
        m_patch{}
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
                L".swap .s - tells which skill the combat attack is swapped to"
            );
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
        }
        else {
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
            ImGui::SetTooltip("Type these in chat (they are not sent to other players):\n.help - list of commands\n.ping - answers pong\n.swap - which skill the combat attack is swapped to");
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
