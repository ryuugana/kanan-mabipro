#include "MessageViewer.hpp"
#include "MabiPacket.h"
#include "imgui.h"
#include "Log.hpp"
#include <sstream>
#include <iomanip>

namespace kanan {
	MessageViewer::MessageViewer()
	{
		m_isEnabled = false;
		m_op.push_back(-1);
	}

	void MessageViewer::onUI() {
		if (ImGui::CollapsingHeader("Message Viewer")) {
			ImGui::TextWrapped("This mod was created for development purposes and sends all incoming messages to logs");
			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
			ImGui::Checkbox("Enable Message Viewer", &m_isEnabled);
		}
	}

	void MessageViewer::onConfigLoad(const Config& cfg) {
		m_isEnabled = cfg.get<bool>("MessageViewer.Enabled").value_or(false);
	}

	void MessageViewer::onConfigSave(Config& cfg) {
		cfg.set<bool>("MessageViewer.Enabled", m_isEnabled);
	}

	void MessageViewer::onRecv(MabiMessage mabiMessage) {
		CMabiPacket recvPacket;
		recvPacket.SetSource(mabiMessage.buffer, mabiMessage.size);

		std::ostringstream ss{};

		string binary;

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
						for each(char hex in binary)
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