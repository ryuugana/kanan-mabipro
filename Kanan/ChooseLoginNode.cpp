#include "ChooseLoginNode.hpp"
#include "MabiPacket.h"
#include "imgui.h"
#include "Log.hpp"

namespace kanan {
	int ChooseLoginNode::m_choice = 0;

	ChooseLoginNode::ChooseLoginNode()
	{
		m_isEnabled = false;
		m_op.push_back(48);
		m_op.push_back(0x4e33);
	}

	void ChooseLoginNode::onUI() {
		if (ImGui::CollapsingHeader("Choose Node")) {
			ImGui::TextWrapped("This mod allows you to choose which node to connect to on login and changing channels.\n\n"
				"This will overwrite the >cc command if used and >server should always show the server chosen.");


			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });

			const char* nodes[] = { "Disabled", "Funf", "Drei", "Test" };
			if (ImGui::Combo("", &m_choice, nodes, IM_ARRAYSIZE(nodes))) {
				m_isEnabled = m_choice > 0;
			}
		}
	}

	void ChooseLoginNode::onConfigLoad(const Config& cfg) {
		m_choice = cfg.get<int>("ChooseLoginNode.Choice").value_or(0);
		m_isEnabled = m_choice > 0;
	}

	void ChooseLoginNode::onConfigSave(Config& cfg) {
		cfg.set<int>("ChooseLoginNode.Choice", m_choice);
	}

	void ChooseLoginNode::onRecv(MabiMessage mabiMessage) {
		CMabiPacket recvPacket;
		recvPacket.SetSource(mabiMessage.buffer, mabiMessage.size);

		PacketData data;
		// Change Connection IP address
		data.type = 6;
		switch (m_choice) {
		case 0:
			return;
		case 1:
			// Funf 75.127.4.123
			data.str = "funf.mabi.pro";
			data.len = (int)strlen("funf.mabi.pro") + 1;
			break;
		case 2:
			// Drei 192.210.185.228
			data.str = "drei.mabi.pro";
			data.len = (int)strlen("drei.mabi.pro") + 1;
			break;
#ifdef TEST
		case 3:
			// For testing only
			data.str = "192.168.100.1";
			data.len = (int)strlen("192.168.100.1") + 1;
			break;
#endif
			return;
		}
		recvPacket.SetElement(&data, 4);
		data.type = 6;
		data.str = "";
		data.len = (int)strlen("") + 1;
		recvPacket.SetElement(&data, 5);
		BYTE* p;
		int tmpSizw = recvPacket.BuildPacket(&p);

		memcpy(mabiMessage.buffer, p, tmpSizw);
		free(p);
	}
}