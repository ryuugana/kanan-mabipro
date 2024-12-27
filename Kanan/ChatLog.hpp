#pragma once

#include <cstdarg>
#include <memory>
#include <vector>

#include <Windows.h>

#include <imgui.h>
#include <fstream>

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
		void addChatLog(const std::string& msg);
		void drawChatLog();

		bool m_fileLogEnabled;
		bool m_startedLogging;
		bool m_isOpen;
		bool m_scrollToBottom;
		bool m_autoScroll;
        ImGuiTextFilter m_filter;
        std::vector<std::string> m_logs;
		std::ofstream m_file;
	};
}