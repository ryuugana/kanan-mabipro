#include "ModChatLog.hpp"

#include <sstream>
#include <ctime>
#include <String.hpp>

#include "dirent.h"
#include "imgui.h"
#include "MabiPacket.h"
#include "ChatLog.hpp"
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


	ModChatLog::ModChatLog()
		: m_fileLogEnabled{ false },
		m_startedLogging{ false }
	{
		m_op = -1;
	}

	void ModChatLog::onUI() {
		if (ImGui::CollapsingHeader("Chat Log")) {
			ImGui::TextWrapped("This mod logs most chat messages when enabled. \n\n"
			"Logged chat messages are sent to View->Show Chat Log and txt files in the \"Kanan Chat Log\" folder in your MabiPro folder. ");
			ImGui::Dummy(ImVec2{ 5.0f, 5.0f });
			if(ImGui::Checkbox("Enable Chat Log", &m_isEnabled))
				startLogging();
		}
	}

	void ModChatLog::onConfigLoad(const Config& cfg) {
		m_isEnabled = cfg.get<bool>("ModChatLog.Enabled").value_or(false);
		//m_fileLogEnabled = cfg.get<bool>("ModChatLog.FileLogEnabled").value_or(false);
		
		if (m_isEnabled)
			startLogging();
	}

	void ModChatLog::onConfigSave(Config& cfg) {
		cfg.set<bool>("ModChatLog.Enabled", m_isEnabled);
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

	unsigned long ModChatLog::onRecv(MabiMessage mabiMessage) {
		unsigned long op = GetOP(mabiMessage.buffer);
		if (!(op == 21100 || op == 21101 || op == 21107 || op == 21109 || op == 36520 || op == 50031)) {
			return op;
		}

		CMabiPacket recvPacket;
		recvPacket.SetSource(mabiMessage.buffer, mabiMessage.size);

		std::ostringstream ss{};

		try {
			if (!std::string(recvPacket.GetElement(1)->str).find("<COMBAT>"))
				return op;

			switch (recvPacket.GetOP())
			{
			case 21100: // All + Personal Shop
				if (recvPacket.GetReciverId() > 4700000000000000 || (recvPacket.GetReciverId() > 0x10010000000000 && recvPacket.GetReciverId() < 0x10020000000000))
					break;
				if (strcmp(recvPacket.GetElement(1)->str, "<PERSONALSHOP>") == 0) {
					ss << getTime() << " | <PERSONALSHOP> " << ": " << recvPacket.GetElement(2)->str;
				}
				else if (strcmp(recvPacket.GetElement(1)->str, "<PARTY>") == 0) {
					ss << getTime() << " | <PARTY> " << ": " << recvPacket.GetElement(2)->str;
				}
				else {
					bool isEmote = false;
					for each(auto emote in emotes) {
						if (strcmp(recvPacket.GetElement(2)->str, emote) == 0) {
							isEmote = true;
							break;
						}
					}
					if(!isEmote)
						ss << getTime() << " | " << recvPacket.GetElement(1)->str << ": " << recvPacket.GetElement(2)->str;
				}
				break;
			case 21101: // System
				if (strcmp(recvPacket.GetElement(1)->str, "Your skill latency reduction value has been detected to be too high. Please lower it..") == 0)
					break;
				if(recvPacket.GetElement(0)->byte8 == 7)
					ss << getTime() << " | <SYSTEM> " << ": " << recvPacket.GetElement(1)->str;
				break;
			case 21107: // Whisper
				ss << getTime() << " | <WHISPER> " << recvPacket.GetElement(0)->str << ": " << recvPacket.GetElement(1)->str;
				break;
			case 21109: // Beginner
				ss << getTime() << " | <GLOBAL> " << recvPacket.GetElement(0)->str << ": " << recvPacket.GetElement(1)->str;
				break;
			case 36520: // Party
				ss << getTime() << " | <PARTY> " << ": " << recvPacket.GetElement(1)->str;
				break;
			case 50031: // Guild
				ss << getTime() << " | <GUILD> " << recvPacket.GetElement(0)->str << ": " << recvPacket.GetElement(1)->str;
				break;
			default:
				break;
			}
			if(ss.str().size() > 0)
				chatLog(ss.str().c_str());
		}
		catch (exception e) {
		}

		return recvPacket.GetOP();
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

	void ModChatLog::startLogging() {
		if (m_startedLogging)
			return;
		else
			m_startedLogging = true;

		if (CreateDirectory(L"Kanan Chat Logs", NULL) ||
			ERROR_ALREADY_EXISTS == GetLastError()) {
			time_t     now = time(0);
			struct tm  tstruct;
			char       buf[80];
			localtime_s(&tstruct, &now);
			strftime(buf, sizeof(buf), "%Y-%m-%d", &tstruct);


			DIR *dir;
			struct dirent *ent;
			string chatLogPath = g_kanan->getPath();
			chatLogPath.append("\\Kanan Chat Logs\\");

			if ((dir = opendir(chatLogPath.c_str())) != NULL) {
				while ((ent = readdir(dir)) != NULL) {
					string str(ent->d_name);
					deleteOldLogs(chatLogPath, str, tstruct);
				}
				closedir(dir);
			}
			else {
				return;
			}

			startChatLog(chatLogPath + buf + ".txt");
		}
	}
}