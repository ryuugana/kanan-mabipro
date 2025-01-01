#pragma once

#include <cstdarg>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <vector>

#include <imgui.h>
#include <Windows.h>

#include "MessageMod.hpp"


namespace kanan {
	class ChatLog : public MessageMod {
	public:
		ChatLog();

		void onUI() override;

		void onWindow() override;

		void onConfigLoad(const Config& cfg) override;
		void onConfigSave(Config& cfg) override;

		void onRecv(MabiMessage mabiMessage) override;
	private:
		void startLogging();
		void deleteOldLogs(std::string path, std::string fileName, tm tstruct);

		void addChatLog(const std::string& msg);
		void drawChatLog();

		std::string ChatLog::getTime();

		bool m_fileLogEnabled;
		bool m_startedLogging;
		bool m_isOpen;
		bool m_scrollToBottom;
		bool m_autoScroll;
		int m_daysToKeepLogs;

        ImGuiTextFilter m_filter;

        std::vector<std::string> m_logs;
		std::ofstream m_file;

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