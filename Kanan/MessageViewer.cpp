#include "MessageViewer.hpp"
#include "MabiPacket.h"
#include "imgui.h"
#include "Log.hpp"
#include <sstream>

namespace kanan {
	MessageViewer::MessageViewer()
	{
		m_funcPtr = onRecv;
		m_isEnabled = false;
		m_op = -1;
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

	unsigned long MessageViewer::onRecv(MabiMessage mabiMessage) {
		CMabiPacket recvPacket;
		recvPacket.SetSource(mabiMessage.buffer, mabiMessage.size);

		std::ostringstream ss{};

		try {
			ss << "OP: " << recvPacket.GetOP() << "\n";
			ss << "ID: " << recvPacket.GetReciverId() << "\n";
			for (int i = 0; i < recvPacket.GetElementNum(); i++) {
				switch (recvPacket.GetElement(i)->type) {
					case T_BYTE: ss << "BYTE: " << (unsigned int)(recvPacket.GetElement(i)->byte8) << "\n"; break;
					case T_SHORT: ss << "SHORT: " << recvPacket.GetElement(i)->word16 << "\n"; break;
					case T_INT: ss << "INT: " << recvPacket.GetElement(i)->int32 << "\n"; break;
					case T_LONG: ss << "LONG: " << recvPacket.GetElement(i)->ID << "\n"; break;
					case T_FLOAT: ss << "FLOAT: " << recvPacket.GetElement(i)->float32 << "\n"; break;
					case T_STRING: ss << "STRING: " << recvPacket.GetElement(i)->str << "\n"; break;
					case T_BIN: ss << "BINARY BLOB: ..." << "\n";
				}
			}
			log("%s\n", ss.str().c_str());
		}
		catch (const char* msg) {
			log(msg);
		}

		return recvPacket.GetOP();
	}
}