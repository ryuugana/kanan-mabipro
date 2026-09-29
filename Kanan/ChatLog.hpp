#pragma once

#include <cstdarg>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <vector>

#include <imgui.h>
#include <Windows.h>

#include <FunctionHook.hpp>
#include <Patch.hpp>

#include "MessageMod.hpp"


namespace kanan {
	class ChatLog : public MessageMod {
	public:
		ChatLog();
		~ChatLog();

		std::string getName() override { return "Chat Mods"; }

		void onUI() override;

		bool onWindow() override;

		void onConfigLoad(const Config& cfg) override;
		void onConfigSave(Config& cfg) override;

		void onRecv(MabiMessage mabiMessage) override;

		// For the chat window hook: the time to put in front of a line, or "" when Add Time to Chat is off.
		std::wstring timePrefix();
	private:
		void startLogging();
		void deleteOldLogs(std::string path, std::string fileName, tm tstruct);

		void addChatLog(const std::string& msg);
		void drawChatLog();

		std::string ChatLog::getTime();

		bool m_fileLogEnabled;
		bool m_startedLogging;
		bool m_isChatLog;
		bool m_isOpen;
		bool m_isTime;
		bool m_is24hour;
		bool m_isAuctionEnabled;
		bool m_isFieldBossEnabled;
		bool m_isFieldBNotifyEnabled;
		bool m_scrollToBottom;
		bool m_autoScroll;
		int m_daysToKeepLogs;

        ImGuiTextFilter m_filter;

        std::vector<std::string> m_logs;
		std::ofstream m_file;

		// Add Time to Chat: in front of the name, where the chat window builds a line (see
		// hookLineText); or, if that code isn't found, in front of the message, through the game's
		// "add a line to the chat window" (pleione::CInterfaceMgr).
		Patch m_linePatch;
		std::unique_ptr<FunctionHook> m_chatLineHook;

		std::unordered_map<long long, std::string> m_partyMembers;

		const char* const ChatLog::m_emotes[10] = {
			"(laugh)",
			"(angry)",
			"(serious)",
			"(jeah)",
			"(confused)",
			"(pain)",
			"(eyesclosed)",
			"(surprised)",
			"(love)",
			"(sad)"
		};
	};
}