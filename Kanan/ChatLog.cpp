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
	ChatLog::ChatLog()
		: m_fileLogEnabled{ false },
		m_startedLogging{ false },
		m_logs{},
		m_filter{},
		m_scrollToBottom{ false },
		m_autoScroll{ true },
		m_isChatLog{ false },
		m_isOpen{ false },
		m_isTime{ false },
		m_is24hour {false},
		m_file{},
		m_partyMembers{}
	{
		m_hasSend = false;
		m_hasRecv = true;
		m_op.push_back(21100);
		m_op.push_back(21101);
		m_op.push_back(21107);
		m_op.push_back(21109);
		m_op.push_back(36502);
		m_op.push_back(36504);
		m_op.push_back(36520);
		m_op.push_back(50031);
	}

	void ChatLog::onUI() {
		if (ImGui::TreeNode(getName().c_str())) {
			ImGui::TextWrapped("This mod logs most chat messages when enabled. \n\n"
			"Logged chat messages are sent to txt files in the \"Kanan Chat Log\" folder in your MabiPro folder for reference. \n\n"
			"Logged chat messages can also be viewed using Show Chat Log, which can be used as an alternative chat window with time stamps.");
			ImGui::Dummy(ImVec2{ 5.0f, 5.0f });
			if(ImGui::Checkbox("Enable Chat Log", &m_isChatLog))
				startLogging();

			ImGui::BeginDisabled(!m_isChatLog);
			ImGui::Checkbox("Show Chat Log", &m_isOpen);
			ImGui::EndDisabled();

			ImGui::Dummy(ImVec2{ 5.0f, 5.0f });

			ImGui::BeginDisabled(!m_isEnabled);
			ImGui::TextWrapped("24-hour clock affects both in-game time and Chat Log time. \n");
			ImGui::Checkbox("Use 24 hour clock", &m_is24hour);
			ImGui::EndDisabled();

			ImGui::Dummy(ImVec2{ 5.0f, 5.0f });

			ImGui::TextWrapped("Displays time in the ingame chat log.\n");
			ImGui::Checkbox("Add Time to Chat", &m_isTime);
			ImGui::TreePop();

			m_isEnabled = m_isChatLog || m_isTime;
		}
	}

	bool ChatLog::onWindow() {
		if (m_isOpen && m_startedLogging && m_isChatLog) {
			drawChatLog();
		}

		return m_isOpen && m_startedLogging && m_isChatLog;
	}

	void ChatLog::onConfigLoad(const Config& cfg) {
		m_isChatLog = cfg.get<bool>("ModChatLog.Enabled").value_or(false);
		m_isOpen = cfg.get<bool>("ChatLog.OpenByDefault").value_or(false);
		m_isTime = cfg.get<bool>("ChatTime.Enabled").value_or(false);
		
		m_isEnabled = m_isTime || m_isChatLog;

		if (m_isChatLog)
			startLogging();
	}

	void ChatLog::onConfigSave(Config& cfg) {
		cfg.set<bool>("ModChatLog.Enabled", m_isChatLog);
		cfg.set<bool>("ChatLog.OpenByDefault", m_isOpen);
		cfg.set<bool>("ChatTime.Enabled", m_isTime);
	}

	std::string ChatLog::getTime() {
		std::ostringstream ss;
		time_t now = time(0);
		tm localTimeNow;
		localtime_s(&localTimeNow, &now);
		std::string hour;
		std::string ampm;

		if (m_is24hour)
		{
			hour = std::to_string(localTimeNow.tm_hour);
		}
		else
		{
			ampm = (localTimeNow.tm_hour >= 12) ? "PM" : "AM";
			int hour12 = localTimeNow.tm_hour % 12;
			if (hour12 == 0) hour12 = 12; // Convert 0 (midnight) or 12 (noon) to 12
			hour = std::to_string(hour12);
		}

		if(localTimeNow.tm_min < 10)
			ss << hour << ":0" << localTimeNow.tm_min << " " << ampm;
		else
			ss << hour << ":" << localTimeNow.tm_min << " " << ampm;
		return ss.str();
	}

	void ChatLog::onRecv(MabiMessage mabiMessage) {
		CMabiPacket recvPacket;
		recvPacket.SetSource(mabiMessage.buffer, mabiMessage.size);

		ostringstream ss{};

		try {
			if (m_isChatLog)
			{
				string message = "";
				switch (recvPacket.GetOP())
				{
				case 21100: // All + Personal Shop
					if (!string(recvPacket.GetElement(1)->str).find("<COMBAT>"))
						return;
					message = recvPacket.GetElement(2)->str;
					if (recvPacket.GetReciverId() > 4700000000000000 || (recvPacket.GetReciverId() > 0x10010000000000 && recvPacket.GetReciverId() < 0x10020000000000))
						return;
					if (strcmp(recvPacket.GetElement(1)->str, "<PERSONALSHOP>") == 0) {
						ss << getTime() << " | <PERSONALSHOP> " << ": " << message;
					}
					else if (strcmp(recvPacket.GetElement(1)->str, "<PARTY>") == 0) {
						ss << getTime() << " | <PARTY> " << ": " << message;
					}
					else {
						bool isEmote = false;
						for each(auto emote in m_emotes) {
							if (message.find(emote) != string::npos)
								return;
						}

						ss << getTime() << " | " << recvPacket.GetElement(1)->str << ": " << message;
					}
					break;
				case 21101: // System
					if (!string(recvPacket.GetElement(1)->str).find("<COMBAT>"))
						return;
					if (strcmp(recvPacket.GetElement(1)->str, "Your skill latency reduction value has been detected to be too high. Please lower it..") == 0)
						return;
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
				case 36502: // Party info
					 m_partyMembers[recvPacket.GetElement(2)->ID] = recvPacket.GetElement(3)->str;
					 break;
				case 36504: // Party info
					for (int i = 14; i < recvPacket.GetElementNum();) {
						log("Party joined elements: %d, i: %d, id: %lld", recvPacket.GetElementNum(), i, recvPacket.GetElement(i)->ID);
						if (recvPacket.GetElement(i)->type == T_LONG) {
							m_partyMembers[recvPacket.GetElement(i)->ID] = recvPacket.GetElement(i + 1)->str;
							i += 11;
						}
						else {
							i += 8;
						}
					}
					break;
				case 36520: // Party
					message = recvPacket.GetElement(1)->str;
					if (m_partyMembers.find(recvPacket.GetElement(0)->ID) == m_partyMembers.end()) {
						ss << getTime() << " | <PARTY> " << ": " << message;
					}
					else {
						ss << getTime() << " | <PARTY> " << m_partyMembers[recvPacket.GetElement(0)->ID] << ": " << message;
					}
					break;
				case 50031: // Guild
					message = recvPacket.GetElement(1)->str;
					ss << getTime() << " | <GUILD> " << recvPacket.GetElement(0)->str << ": " << message;
					break;
				default:
					break;
				}
				if (ss.str().size() > 0)
				{
					std::string log = ss.str();
					std::replace(log.begin(), log.end(), '%', 'p');
					addChatLog(log.c_str());
				}
			}

			if (m_isTime)
			{
				int op = recvPacket.GetOP();
				std::string addTime;
				int index = 1;

				if (recvPacket.GetElement(1)->type == T_STRING && recvPacket.GetElement(1)->len > 0 && !string(recvPacket.GetElement(1)->str).find("<COMBAT>"))
					return;
				else if (op == 21101)
				{
					if (recvPacket.GetElement(0)->byte8 != 7)
					{
						return;
					}
				}

				if (op == 21100)
				{
					addTime = recvPacket.GetElement(index)->str;
					addTime.append(" [" + getTime() + ']');
				}
				else if (op == 36502 || op == 36504)
				{
					return;
				}
				else
				{
					addTime = '[' + getTime() + "] ";
					addTime.append(recvPacket.GetElement(index)->str);
				}

				PacketData data;
				data.type = T_STRING;
				data.str = addTime.data();
				data.len = addTime.length();
				recvPacket.SetElement(&data, index);

				BYTE* p;
				int tmpSizw = recvPacket.BuildPacket(&p);

				MabiMessage newMsg;
				newMsg.buffer = p;
				newMsg.size = tmpSizw;
				AddToRecvQ(newMsg);

			    memset(mabiMessage.buffer, 0, mabiMessage.size);
			}
		}
		catch (exception e) {
		}
	}

void ChatLog::deleteOldLogs(string path, string fileName, tm tstruct) {
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
		if (month != tstruct.tm_mon + 1) {
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
		ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

		if (m_filter.IsActive()) {
			for (auto log : m_logs)
			{
				if (m_filter.PassFilter(log.c_str())) {
					ImGui::TextWrapped(log.c_str());
				}
			}
		}
		else {
			for (auto log : m_logs)
			{
				ImGui::TextWrapped(log.c_str());
			}
		}

		if (m_scrollToBottom && m_autoScroll) {
			ImGui::SetScrollHereY(1.0f);
		}

		m_scrollToBottom = false;

		ImGui::EndChild();
		ImGui::End();
	}
}