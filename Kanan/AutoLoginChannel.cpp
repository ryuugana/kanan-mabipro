#include "AutoLoginChannel.hpp"
#include "MabiPacket.h"
#include "imgui.h"
#include "Log.hpp"
#include "MabiMessageHook.hpp"
#include "Game.hpp"
#include "Kanan.hpp"

namespace kanan {
	bool sendTest = false;
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
		if (ImGui::TreeNode("Auto Login Channel")) {
			ImGui::TextWrapped("This mod will automatically log you into the chosen channel.\n"
				"This will ignore the channel choice in game and force the channel below.");


			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });

			if (ImGui::Combo("Channel", &m_choice, m_channels.data(), m_channels.size())) {
				m_isEnabled = m_choice > 0;
			}
			ImGui::TreePop();
		}
	}

	void AutoLoginChannel::onConfigLoad(const Config& cfg) {
		m_choice = cfg.get<int>("AutoLoginChannel.Choice").value_or(0);
		m_isEnabled = m_choice > 0;
	}

	void AutoLoginChannel::onConfigSave(Config& cfg) {
		cfg.set<int>("AutoLoginChannel.Choice", m_choice);
	}

	void AutoLoginChannel::onSend(MabiMessage mabiMessage) {
		if (m_choice < 1)
		{
			return;
		}

		CMabiPacket sendPacket;
		sendPacket.SetSource(mabiMessage.buffer, mabiMessage.size);
		
		if (strcmp("Housing", sendPacket.GetElement(1)->str) != 0)
		{
			PacketData data;
			data.type = 6;

			// Set Channel
			data.str = m_channels[m_choice];
			data.len = (int)strlen(m_channels[m_choice]) + 1;

			sendPacket.SetElement(&data, 1);
			BYTE* p;
			int tmpSizw = sendPacket.BuildPacket(&p);

			memcpy(mabiMessage.buffer, p, tmpSizw);
			free(p);
		}
	}
}