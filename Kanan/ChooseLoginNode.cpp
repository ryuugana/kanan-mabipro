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
	}

	void ChooseLoginNode::onUI() {
		if (ImGui::CollapsingHeader("Choose Login Node")) {
			ImGui::TextWrapped("This mod allows you to choose which node to connect to on login.");


			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });

			const char* nodes[] = { "Disabled", "Vier", "Funf", "Drei" };
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
			break;
		/*case 1:
			// Vier
			data.str = "162.253.176.221";
			data.len = (int)strlen("162.253.176.221") + 1;
			break;*/
		case 2:
			// Funf
			data.str = "75.127.4.123";
			data.len = (int)strlen("75.127.4.123") + 1;
			break;
		case 3:
			// Drei
			data.str = "192.210.185.228";
			data.len = (int)strlen("192.210.185.228") + 1;
			break;
		}
		recvPacket.SetElement(&data, 4);
		data.type = 6;
		data.str = "";
		data.len = (int)strlen("") + 1;
		recvPacket.SetElement(&data, 5);
		BYTE* p;
		int tmpSizw = recvPacket.BuildPacket(&p);

		memcpy(mabiMessage.buffer, p, tmpSizw);
	}
}