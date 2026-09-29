#pragma once

#include <functional>
#include <string>

#include <Patch.hpp>

#include "PatchMod.hpp"

namespace kanan {
    // Chat commands that start with a dot, answered in the chat window and never sent to the
    // server. Ported from AstralWorld's User Commands:
    //   .help / .h   lists the commands
    //   .ping / .p   answers "pong"
    //   .swap / .s   tells which skill the combat attack is swapped to (needs a provider, see
    //                setCombatSwapQuery; AstralWorld asked its Combat Mastery Swap patch)
    //   .reload / .r AstralWorld re-read mss32.ini; Kanan applies setting changes immediately, so
    //                this only explains where to change them.
    //   .price       where the traveling merchant Price is and how long until he moves
    //   .rua / .fleta / .tarlach  where they are (Bean Rua or home; out in Sen Mag or not; a man or
    //                a bear) and how long until that changes, from their server scripts
    //   .weather     the weather where the player is and what comes next (MabiTrackers)
    //   .tracker     opens the Erinn Tracker page (MabiTrackers): weather, moon gates, and Price,
    //                Rua, Fleta and Tarlach's schedules
    //
    // Price's location is worked out the way the server's script does it (GetTargetPosition in
    // npc/common.mint): he stays at stop (Erinn day % 14) of a fixed rotation, and moves when a new
    // Erinn day starts (every 36 minutes), on his next pulse (30-40 seconds later at most).
    //
    // The game's chat input function (the one that handles "/" commands) is hooked at its start, and
    // so is the chat send filter in front of it, so its "skip repeated messages" and flood checks
    // don't apply to commands.
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
        Patch m_filterPatch;    // the chat send filter, so its repeat/flood checks skip commands

        void apply();
        void printToChat(const std::wstring& message);
    };
}
