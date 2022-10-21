#include "ModChatLog.hpp"
#include "MabiPacket.h"
#include "imgui.h"
#include "ChatLog.hpp"
#include <sstream>
#include <ctime>
#include <String.hpp>

namespace kanan {
	bool ModChatLog::m_fileLogEnabled;
	ModChatLog::ModChatLog()
	{
		m_fileLogEnabled = false;
		m_funcPtr = onRecv;
		m_isEnabled = false;
		m_op = -1;
	}

	void ModChatLog::onUI() {
		if (ImGui::CollapsingHeader("Chat Log")) {
			ImGui::TextWrapped("This mod logs most chat messages when enabled. \n\n"
			"Logged chat messages are sent to View->Show Chat Log and a file named \"kananChatLog.txt\" in your MabiPro folder. ");
			ImGui::Dummy(ImVec2{ 5.0f, 5.0f });
			ImGui::Checkbox("Enable Chat Log", &m_isEnabled);
		}
	}

	void ModChatLog::onConfigLoad(const Config& cfg) {
		m_isEnabled = cfg.get<bool>("ModChatLog.Enabled").value_or(false);
		m_fileLogEnabled = cfg.get<bool>("ModChatLog.FileLogEnabled").value_or(false);
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
					ss << getTime() << " - <PERSONALSHOP> " << ": " << recvPacket.GetElement(2)->str;
				}
				else if (strcmp(recvPacket.GetElement(1)->str, "<PARTY>") == 0) {
					ss << getTime() << " - <PARTY> " << ": " << recvPacket.GetElement(2)->str;
				}
				else {
					ss << getTime() << " - " << recvPacket.GetElement(1)->str << ": " << recvPacket.GetElement(2)->str;
				}
				break;
			case 21101: // System
				if (strcmp(recvPacket.GetElement(1)->str, "Your skill latency reduction value has been detected to be too high. Please lower it..") == 0)
					break;
				if(recvPacket.GetElement(0)->byte8 == 7)
					ss << getTime() << " - <SYSTEM> " << ": " << recvPacket.GetElement(1)->str;
				break;
			case 21107: // Whisper
				ss << getTime() << " - <WHISPER> " << recvPacket.GetElement(0)->str << ": " << recvPacket.GetElement(1)->str;
				break;
			case 21109: // Beginner
				ss << getTime() << " - <GLOBAL> " << recvPacket.GetElement(0)->str << ": " << recvPacket.GetElement(1)->str;
				break;
			case 36520: // Party
				ss << getTime() << " - <PARTY> " << ": " << recvPacket.GetElement(1)->str;
				break;
			case 50031: // Guild
				ss << getTime() << " - <GUILD> " << recvPacket.GetElement(0)->str << ": " << recvPacket.GetElement(1)->str;
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
}