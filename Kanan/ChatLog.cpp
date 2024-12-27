#include "ChatLog.hpp"

#include <sstream>
#include <ctime>
#include <String.hpp>

#include "dirent.h"
#include "imgui.h"
#include "MabiPacket.h"
#include "Log.hpp"
#include "Kanan.hpp"

namespace kanan {
	const char* const emotes[] = {
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


	ChatLog::ChatLog()
		: m_fileLogEnabled{ false },
		m_startedLogging{ false },
		m_logs{},
		m_filter{},
		m_scrollToBottom{ false },
		m_autoScroll{ true },
		m_isOpen{ false },
		m_file{}
	{
		m_op.push_back(21100);
		m_op.push_back(21101);
		m_op.push_back(21107);
		m_op.push_back(21109);
		m_op.push_back(36520);
		m_op.push_back(50031);
	}

	void ChatLog::onUI() {
		if (ImGui::CollapsingHeader("Chat Log")) {
			ImGui::TextWrapped("This mod logs most chat messages when enabled. \n\n"
			"Logged chat messages are sent to View->Show Chat Log and txt files in the \"Kanan Chat Log\" folder in your MabiPro folder. \n\n"
			"It can also be used as an alternative chat window with time stamps.");
			ImGui::Dummy(ImVec2{ 5.0f, 5.0f });
			if(ImGui::Checkbox("Enable Chat Log", &m_isEnabled))
				startLogging();
			if (m_isEnabled)
			{
				ImGui::Checkbox("Show Chat Log", &m_isOpen);
			}
		}
	}

	void ChatLog::onWindow() {
		if (m_isOpen && m_startedLogging && m_isEnabled) {
			drawChatLog();
		}
	}

	void ChatLog::onConfigLoad(const Config& cfg) {
		m_isEnabled = cfg.get<bool>("ModChatLog.Enabled").value_or(false);
		m_isOpen = cfg.get<bool>("ChatLog.OpenByDefault").value_or(false);
		//m_fileLogEnabled = cfg.get<bool>("ChatLog.FileLogEnabled").value_or(false);
		
		if (m_isEnabled)
			startLogging();
	}

	void ChatLog::onConfigSave(Config& cfg) {
		cfg.set<bool>("ModChatLog.Enabled", m_isEnabled);
		cfg.set<bool>("ChatLog.OpenByDefault", m_isOpen);
	}

	std::string getTime() {
		std::ostringstream ss;
		time_t now = time(0);
		tm localTimeNow;
		localtime_s(&localTimeNow, &now);
		if(localTimeNow.tm_min < 10)
			ss << localTimeNow.tm_hour << ":0" << localTimeNow.tm_min;
		else
			ss << localTimeNow.tm_hour << ":" << localTimeNow.tm_min;
		return ss.str();
	}

	string removePercent(string message) {
		int pos = message.find("%");
		while (pos != string::npos) {
			message = message.replace(pos, 1, "p");
			pos = message.find("%");
		}
		return message;
	}

	void ChatLog::onRecv(MabiMessage mabiMessage) {
		unsigned long op = GetOP(mabiMessage.buffer);

		CMabiPacket recvPacket;
		recvPacket.SetSource(mabiMessage.buffer, mabiMessage.size);

		ostringstream ss{};

		try {
			if (!string(recvPacket.GetElement(1)->str).find("<COMBAT>"))
				return;
			string message = "";
			switch (recvPacket.GetOP())
			{
			case 21100: // All + Personal Shop
				message = recvPacket.GetElement(2)->str;
				if (recvPacket.GetReciverId() > 4700000000000000 || (recvPacket.GetReciverId() > 0x10010000000000 && recvPacket.GetReciverId() < 0x10020000000000))
					break;
				if (strcmp(recvPacket.GetElement(1)->str, "<PERSONALSHOP>") == 0) {
					ss << getTime() << " | <PERSONALSHOP> " << ": " << message;
				}
				else if (strcmp(recvPacket.GetElement(1)->str, "<PARTY>") == 0) {
					ss << getTime() << " | <PARTY> " << ": " << message;
				}
				else {
					bool isEmote = false;
					for each(auto emote in emotes) {
						if (message.find(emote) != string::npos)
							return;
					}

					ss << getTime() << " | " << recvPacket.GetElement(1)->str << ": " << message;
				}
				break;
			case 21101: // System
				if (strcmp(recvPacket.GetElement(1)->str, "Your skill latency reduction value has been detected to be too high. Please lower it..") == 0)
					break;
				message = recvPacket.GetElement(1)->str;
				if (recvPacket.GetElement(0)->byte8 == 7)
					ss << getTime() << " | <SYSTEM> " << ": " << recvPacket.GetElement(1)->str;
				break;
			case 21107: // Whisper
				message = recvPacket.GetElement(1)->str;
				ss << getTime() << " | <WHISPER> " << recvPacket.GetElement(0)->str << ": " << recvPacket.GetElement(1)->str;
				break;
			case 21109: // Beginner
				message = recvPacket.GetElement(1)->str;
				ss << getTime() << " | <GLOBAL> " << recvPacket.GetElement(0)->str << ": " << recvPacket.GetElement(1)->str;
				break;
			case 36520: // Party
				message = recvPacket.GetElement(1)->str;
				ss << getTime() << " | <PARTY> " << ": " << recvPacket.GetElement(1)->str;
				break;
			case 50031: // Guild
				message = recvPacket.GetElement(1)->str;
				ss << getTime() << " | <GUILD> " << recvPacket.GetElement(0)->str << ": " << recvPacket.GetElement(1)->str;
				break;
			default:
				break;
			}
			if (ss.str().size() > 0)
				addChatLog(ss.str().c_str());
		}
		catch (exception e) {
		}
	}

	void deleteOldLogs(string path, string fileName, struct tm  tstruct) {
		int year = 0;
		int month = 0;
		int day = 0;
		int pos = 0;
		string str;
		path.append(fileName);

		// Get date from text file
		while (year == 0 || month == 0 || day == 0) {
			str = fileName.substr(pos, fileName.length() - 1);

			size_t i = 0;
			for (; i < str.length(); i++) {
				if (!isdigit(str[i]) && isdigit(str[i - 1]))
					break;
			}
			if (i >= str.length())
				return;

			pos += i + 1;
			str = str.substr(0, i);

			if (year == 0) {
				year = atoi(str.c_str());
			}
			else if (month == 0) {
				month = atoi(str.c_str());
			}
			else if (day == 0) {
				day = atoi(str.c_str());
			}
		}

		// Delete file if it's older than a week
		if (year < tstruct.tm_year) {
			if (month == 12) {
				if ((tstruct.tm_mday - 7) + 31 > day)
					std::remove(path.c_str());
			}
			else
				std::remove(path.c_str());
		}
		else if (month < tstruct.tm_mon + 1) {
			switch (month) {
			case 1:
			case 3:
			case 5:
			case 7:
			case 8:
			case 10:
			case 12:
				if ((tstruct.tm_mday - 7) + 31 > day)
					std::remove(path.c_str());
				break;
			case 2:
				if ((tstruct.tm_mday - 7) + 28 > day)
					std::remove(path.c_str());
				break;
			case 4:
			case 6:
			case 9:
			case 11:
				if ((tstruct.tm_mday - 7) + 30 > day)
					std::remove(path.c_str());
				break;
			default:
				break;
			}
		}
		else if ((tstruct.tm_mday - 7) > day) {
			std::remove(path.c_str());
		}
	}

	void ChatLog::startLogging() {
		if (m_startedLogging)
			return;

		if (CreateDirectory(L"Kanan Chat Logs", NULL) ||
			ERROR_ALREADY_EXISTS == GetLastError()) {
			time_t     now = time(0);
			struct tm  tstruct;
			char       buf[80];
			localtime_s(&tstruct, &now);
			strftime(buf, sizeof(buf), "%Y-%m-%d", &tstruct);


			DIR *dir;
			struct dirent *ent;
			string chatLogFolder = g_kanan->getPath();
			chatLogFolder.append("\\Kanan Chat Logs\\");

			if ((dir = opendir(chatLogFolder.c_str())) != NULL) {
				while ((ent = readdir(dir)) != NULL) {
					string str(ent->d_name);
					deleteOldLogs(chatLogFolder, str, tstruct);
				}
				closedir(dir);
			}
			else {
				return;
			}

			string chatLogPath = chatLogFolder + buf + ".txt";

			m_file.open(chatLogPath, std::ios::out | std::ios::app);
			if (m_file.fail())
				return;
			m_file.exceptions(m_file.exceptions() | std::ios::failbit | std::ifstream::badbit);

			m_startedLogging = true;

			log("Started logging chat in %s", chatLogPath);
		}
	}

	void ChatLog::addChatLog(const string& msg) {
		m_logs.push_back(msg);

		m_scrollToBottom = true;

		m_file << msg << std::endl;
	}

	void ChatLog::drawChatLog() {
		ImGui::SetNextWindowSize(ImVec2{ 500.0f, 400.0f }, ImGuiCond_FirstUseEver);

		if (!ImGui::Begin("Chat Log", &m_isOpen, ImGuiWindowFlags_NoFocusOnAppearing)) {
			ImGui::End();
			return;
		}

		ImGui::Checkbox("Auto Scroll  ", &m_autoScroll);

		ImGui::SameLine();
		m_filter.Draw("Filter", -100.0f);
		ImGui::Separator();
		ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

		if (m_filter.IsActive()) {
			for (auto log : m_logs)
			{
				if (m_filter.PassFilter(log.c_str())) {
					ImGui::TextWrappedNoFmt(log.c_str());
				}
			}
			m_scrollToBottom = false;
		}
		else {
			for (auto log : m_logs)
			{
				ImGui::TextWrappedNoFmt(log.c_str());
			}
		}

		if (m_scrollToBottom && m_autoScroll) {
			ImGui::SetScrollHere(1.0f);
		}

		m_scrollToBottom = false;

		ImGui::EndChild();
		ImGui::End();
	}
}