#include "MessageViewer.hpp"

#include <sstream>
#include <iomanip>

#include "imgui.h"
#include "Log.hpp"
#include "Kanan.hpp"
#include "MabiPacket.h"

namespace kanan {
	MessageViewer::MessageViewer()
	{
		m_hasSend = true;
		m_hasRecv = true;
		m_isEnabled = false;
		m_logMsgs = false;
		m_op.push_back(-1);
	}

	void MessageViewer::onUI() {
		if (ImGui::TreeNode("Message Viewer")) {
			ImGui::TextWrapped("This mod was created for development purposes and sends all incoming messages to MabiPale and Kanan logs");
			ImGui::TextWrapped("Requires a Kanan config save to initialize on first enable.");

			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });

			ImGui::Checkbox("Enable Message Viewer", &m_isEnabled);

			ImGui::BeginDisabled(!m_isEnabled);
			ImGui::Checkbox("Record all messages to Kanan log", &m_logMsgs);
			ImGui::EndDisabled();

			ImGui::TreePop();
		}
	}

	void MessageViewer::onConfigLoad(const Config& cfg) {
		m_isEnabled = cfg.get<bool>("MessageViewer.Enabled").value_or(false);
		m_logMsgs = cfg.get<bool>("MessageLogger.Enabled").value_or(false);

		if (m_isEnabled)
		{
			log("Initialize wps: %d", m_wps.Initialize(g_kanan->getHModule()));
		}
	}

	void MessageViewer::onConfigSave(Config& cfg) {
		cfg.set<bool>("MessageViewer.Enabled", m_isEnabled);
		cfg.set<bool>("MessageLogger.Enabled", m_logMsgs);

		if (m_isEnabled)
		{
			log("Initialize wps: %d\n", m_wps.Initialize(g_kanan->getHModule()));
		}
	}

	void MessageViewer::onSend(MabiMessage mabiMessage) {
		viewMessage(mabiMessage, true);

		m_wps.SendToClient(Sign::Send, mabiMessage.buffer, mabiMessage.size);
	}

	void MessageViewer::onRecv(MabiMessage mabiMessage) {
		viewMessage(mabiMessage, false);

		m_wps.SendToClient(Sign::Recv, mabiMessage.buffer, mabiMessage.size);
	}

	void MessageViewer::viewMessage(MabiMessage mabiMessage, bool isSend)
	{
		if (!m_logMsgs) return;
		CMabiPacket recvPacket;
		recvPacket.SetSource(mabiMessage.buffer, mabiMessage.size);

		std::ostringstream ss{};

		string binary;

		if (isSend)
		{
			log("Sending:");
		}
		else
		{
			log("Receiving:");
		}

		try {
			ss << "OP: " << std::hex << recvPacket.GetOP() << "\n";
			ss << "ID: " << recvPacket.GetReciverId() << std::dec << "\n";
			for (int i = 0; i < recvPacket.GetElementNum(); i++) {
				switch (recvPacket.GetElement(i)->type) {
				case T_BYTE: ss << "BYTE: " << (unsigned int)(recvPacket.GetElement(i)->byte8) << "\n"; break;
				case T_SHORT: ss << "SHORT: " << recvPacket.GetElement(i)->word16 << "\n"; break;
				case T_INT: ss << "INT: " << recvPacket.GetElement(i)->int32 << "\n"; break;
				case T_LONG: ss << std::hex << "LONG: " << recvPacket.GetElement(i)->ID << std::dec << "\n"; break;
				case T_FLOAT: ss << "FLOAT: " << recvPacket.GetElement(i)->float32 << "\n"; break;
				case T_STRING: ss << "STRING: " << recvPacket.GetElement(i)->str << "\n"; break;
				case T_BIN:
					binary = recvPacket.GetElement(i)->str;
					ss << "BINARY: ";
					for each (char hex in binary)
					{
						ss << std::hex << std::setfill('0') << std::setw(2) << (unsigned int)std::uint8_t(hex) << std::dec;
					}
					ss << "\n";
					break;
				default:
					break;
				}
			}
			log("%s\n", ss.str().c_str());
		}
		catch (const char* msg) {
			log(msg);
		}
	}
}