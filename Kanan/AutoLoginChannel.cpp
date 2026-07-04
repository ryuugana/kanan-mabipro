#include "AutoLoginChannel.hpp"
#include "MabiPacket.h"
#include "imgui.h"
#include "Log.hpp"

namespace kanan {
	int AutoLoginChannel::m_choice = 0;

	AutoLoginChannel::AutoLoginChannel()
	{
		m_hasSend = true;
		m_hasRecv = false;
		m_isEnabled = false;

		m_op.push_back(0x2f);

		m_channels.push_back("Disabled");
		m_channels.push_back("Channel 1");
		m_channels.push_back("Channel 2");
	}

	void AutoLoginChannel::onUI() {
		if (ImGui::CollapsingHeader("Auto Login Channel")) {
			ImGui::TextWrapped("This mod will automatically log you into the chosen channel.\n"
				"This will ignore the channel choice in game and force the channel below.");


			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });

			if (ImGui::Combo("Channel", &m_choice, m_channels.data(), m_channels.size())) {
				m_isEnabled = m_choice > 0;
			}
		}
	}

	void AutoLoginChannel::onConfigLoad(const Config& cfg) {
		m_choice = cfg.get<int>("AutoLoginChannel.Choice").value_or(0);
		m_isEnabled = m_choice > 0;
	}

	void AutoLoginChannel::onConfigSave(Config& cfg) {
		cfg.set<int>("AutoLoginChannel.Choice", m_choice);
	}

	void AutoLoginChannel::onRecv(MabiMessage mabiMessage) {
		CMabiPacket recvPacket;
		recvPacket.SetSource(mabiMessage.buffer, mabiMessage.size);

		PacketData data;
		// Set Channel
		data.type = 6;
		switch (m_choice) {
		case 0:
			return;
		case 1:
			data.str = m_channels[1];
			data.len = (int)strlen(m_channels[1]) + 1;
			break;
		case 2:
			data.str = m_channels[2];
			data.len = (int)strlen(m_channels[2]) + 1;
			break;
		default:
			return;
		}
		recvPacket.SetElement(&data, 1);
		BYTE* p;
		int tmpSizw = recvPacket.BuildPacket(&p);

		memcpy(mabiMessage.buffer, p, tmpSizw);
		free(p);
	}
}