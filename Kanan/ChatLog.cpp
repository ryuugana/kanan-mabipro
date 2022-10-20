#include "ChatLog.hpp"
#include "MabiPacket.h"
#include "imgui.h"
#include "Log.hpp"
#include <sstream>

namespace kanan {
	ChatLog::ChatLog()
	{
		m_funcPtr = onRecv;
		m_isEnabled = false;
		m_op = -1;
	}

	void ChatLog::onUI() {
		if (ImGui::CollapsingHeader("Message Viewer")) {
			ImGui::Text("This mod was created for development purposes and will send all incoming messages to logs");
			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
			ImGui::Checkbox("Enable Message Viewer", &m_isEnabled);
		}
	}

	void ChatLog::onConfigLoad(const Config& cfg) {
		m_isEnabled = cfg.get<bool>("ChatLog.Enabled").value_or(false);
	}

	void ChatLog::onConfigSave(Config& cfg) {
		cfg.set<bool>("ChatLog.Enabled", m_isEnabled);
	}

	unsigned long ChatLog::onRecv(MabiMessage mabiMessage) {
		CMabiPacket recvPacket;
		recvPacket.SetSource(mabiMessage.buffer, mabiMessage.size);

		std::ostringstream ss{};

		try {
			ss << "OP: " << recvPacket.GetOP() << "\n";
			ss << "ID: " << recvPacket.GetReciverId() << "\n";
			ss << "Elements: " << recvPacket.GetElementNum() << "\n";
			for (int i = 0; i < recvPacket.GetElementNum(); i++) {
				switch (recvPacket.GetElement(i)->type) {
				case T_BYTE: ss << "BYTE: " << "?" << "\n"; break;
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