#include "BlockSpam.hpp"
#include "MabiPacket.h"
#include "imgui.h"
#include "Log.hpp"

namespace kanan {
	BlockSpam::BlockSpam()
	{
		m_isEnabled = false;
		m_isBSEnabled = false;
		m_isBOEEnabled = false;
		m_op.push_back(21101);
		m_op.push_back(21103);
	}

	void BlockSpam::onUI() {
		if (ImGui::CollapsingHeader("Block Spam")) {
			ImGui::TextWrapped("The following mod blocks the following message:\n"
				"\"You are over encumbered. Please clean out your Temporary Inventory.\"\n"
				"Note that this will not stop the status, it will only block the message from appearing in the center of the screen.");
			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
			ImGui::Checkbox("Enable Block Over Encumbered Spam", &m_isBOEEnabled);
			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
			ImGui::TextWrapped("The following mod blocks >skill from spamming the following message:\n"
			"\"Your skill latency reduction value has been detected to be too high. Please lower it..\"\n"
			"This mod was made because usually >skill will spam you whether you use it correctly or not");
			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
			ImGui::Text("How to use >skill command in game:");
			ImGui::TextWrapped(">skill is a command part of MabiPro that slightly decreases skill load time\n"
			">skill is designed to be used with your ping to the node.\n"
			"Example: type >skill 102 if your ping to funf.mabi.pro is 102 and you are on Vier\n"
			">server will tell you which node you are on\n"
			">skill 0 or lower will disable the command");
			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
			ImGui::Checkbox("Enable Block >skill Spam", &m_isBSEnabled);
		}
	}

	void BlockSpam::onConfigLoad(const Config& cfg) {
		m_isBSEnabled = cfg.get<bool>("BlockSpam.Enabled").value_or(false);
		m_isBOEEnabled = cfg.get<bool>("BlockSpam.Enabled").value_or(false);
		m_isEnabled = m_isBOEEnabled || m_isBSEnabled;
	}

	void BlockSpam::onConfigSave(Config& cfg) {
		cfg.set<bool>("BlockSpam.Enabled", m_isBSEnabled);
		cfg.set<bool>("BlockSpam.Enabled", m_isBOEEnabled);
		m_isEnabled = m_isBOEEnabled || m_isBSEnabled;
	}

	void BlockSpam::onRecv(MabiMessage mabiMessage) {
		CMabiPacket recvPacket;
		recvPacket.SetSource(mabiMessage.buffer, mabiMessage.size);

		if (m_isBSEnabled && recvPacket.GetOP() == 21101)
		{
			if (strcmp(recvPacket.GetElement(1)->str, "Your skill latency reduction value has been detected to be too high. Please lower it..") == 0) 
			{

				// Hide >skill spam
				PacketData data;
				data.type = 1;
				data.byte8 = 0;
				recvPacket.SetElement(&data, 0);
				BYTE* p;
				int tmpSizw = recvPacket.BuildPacket(&p);

				memcpy(mabiMessage.buffer, p, tmpSizw);
			}
		}
		else if (m_isBOEEnabled && recvPacket.GetOP() == 21103)
		{
			if (strcmp(recvPacket.GetElement(0)->str, "You are over encumbered. Please clean out your Temporary Inventory.") == 0)
			{
				memset(mabiMessage.buffer, 0, mabiMessage.size);
			}
		}
	}
}