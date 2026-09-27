#pragma once

#include <functional>
#include <string>

#include <Patch.hpp>

#include "PatchMod.hpp"

namespace kanan {
    // Chat commands that start with a dot, answered in the chat window and never sent to the
    // server. Ported from Fantasia's User Commands:
    //   .help / .h   lists the commands
    //   .ping / .p   answers "pong"
    //   .swap / .s   tells which skill the combat attack is swapped to (needs a provider, see
    //                setCombatSwapQuery; Fantasia asked its Combat Mastery Swap patch)
    //   .reload / .r Fantasia re-read mss32.ini; Kanan applies setting changes immediately, so
    //                this only explains where to change them.
    //
    // The game's chat input function (the one that handles "/" commands) is hooked at its start.
    // A message is only taken as a command when it is a dot followed by a word (".help", ".s");
    // other messages that start with a dot ("...", ". ok") are sent as usual.
    class UserCommands : public PatchMod {
    public:
        UserCommands();
        ~UserCommands();

        void onPatchUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

        // Lets another mod answer .swap: return the skill ID the combat attack is swapped to, or 0.
        static void setCombatSwapQuery(std::function<int()> query);

        // Called by the hook with the chat message. Returns true when it was a command.
        bool onChatInput(const wchar_t* message);

    private:
        bool m_enabled;
        bool m_isAvailable;
        Patch m_patch;

        void apply();
        void printToChat(const std::wstring& message);
    };
}
