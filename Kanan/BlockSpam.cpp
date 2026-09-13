#include "BlockSpam.hpp"
#include "MabiPacket.h"
#include "imgui.h"
#include "Log.hpp"

namespace kanan {
	BlockSpam::BlockSpam()
	{
		m_hasSend = false;
		m_hasRecv = true;
		m_isEnabled = false;
		m_isBSEnabled = false;
		m_isBOEEnabled = false;
		m_isBECEnabled = false;
		m_isBSEnabled = false;
		m_op.push_back(0x526D);
		m_op.push_back(0x526F);
	}

	void BlockSpam::onUI() 
	{
		if (ImGui::TreeNode(getName().c_str())) {
		{
			if (ImGui::TreeNode("Exploration Cap"))
			{
				ImGui::TextWrapped("Blocks the following message:\n"
					"\"In order to reach the next exploration level, you must complete an Exploration Cap Quest.\nLook for Mandatory(Red) Quests on the Exploration Quest Board.\"\n");
				ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
				ImGui::Checkbox("Enable Block Exploration Cap Spam", &m_isBECEnabled);
				ImGui::TreePop();
			}
			if (ImGui::TreeNode("Over Encumbered"))
			{
				ImGui::TextWrapped("Blocks the following message:\n"
					"\"You are over encumbered. Please clean out your Temporary Inventory.\"\n"
					"Note that this will not stop the status, it will only block the message from appearing in the center of the screen.");
				ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
				ImGui::Checkbox("Enable Block Over Encumbered Spam", &m_isBOEEnabled);
				ImGui::TreePop();
			}
			if (ImGui::TreeNode(">skill"))
			{
				ImGui::TextWrapped("Blocks >skill from spamming the following message:\n"
					"\"Your skill latency reduction value has been detected to be too high. Please lower it..\"\n"
					"This mod was made because usually >skill will spam you whether you use it correctly or not");
				ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
				ImGui::Text("How to use >skill command in game:");
				ImGui::TextWrapped(">skill is a command part of MabiPro that slightly decreases skill lag\n"
					">skill is designed to be used with your ping to the node.\n"
					"Example: type >skill 102 if your ping to funf.mabi.pro is 102 and you are on Funf\n"
					">server will tell you which node you are on\n"
					">skill 0 or lower will disable the command");
				ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
				ImGui::Checkbox("Enable Block >skill Spam", &m_isBSEnabled);
				ImGui::TreePop();
			}
			if (ImGui::TreeNode("Welcome Messages"))
			{
				ImGui::TextWrapped("Blocks the following related messages:\n"
					"\"Welcome to MabiPro! Have fun and enjoy your stay!\"\n");
				ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
				ImGui::Checkbox("Enable Block Welcome Messages", &m_isBWMEnabled);
				ImGui::TreePop();
			}
			ImGui::TreePop();
		}
	}

	void BlockSpam::onConfigLoad(const Config& cfg) {
		m_isBSEnabled = cfg.get<bool>("BlockSpam.Enabled").value_or(false);
		m_isBOEEnabled = cfg.get<bool>("BlockOverEnc.Enabled").value_or(false);
		m_isBECEnabled = cfg.get<bool>("BlockExplorationCap.Enabled").value_or(false);
		m_isBWMEnabled = cfg.get<bool>("BlockWelcomeMessages.Enabled").value_or(false);
		m_isEnabled = m_isBOEEnabled || m_isBSEnabled || m_isBECEnabled || m_isBWMEnabled;
	}

	void BlockSpam::onConfigSave(Config& cfg) {
		cfg.set<bool>("BlockSpam.Enabled", m_isBSEnabled);
		cfg.set<bool>("BlockOverEnc.Enabled", m_isBOEEnabled);
		cfg.set<bool>("BlockExplorationCap.Enabled", m_isBECEnabled);
		cfg.set<bool>("BlockWelcomeMessages.Enabled", m_isBWMEnabled);
		m_isEnabled = m_isBOEEnabled || m_isBSEnabled || m_isBECEnabled || m_isBWMEnabled;
	}

	void BlockSpam::onRecv(MabiMessage mabiMessage) {
		CMabiPacket recvPacket;
		recvPacket.SetSource(mabiMessage.buffer, mabiMessage.size);

		if ((m_isBSEnabled || m_isBECEnabled || m_isBWMEnabled) && recvPacket.GetOP() == 0x526D)
		{
			std::string message = recvPacket.GetElement(1)->str;

			if (message.length() >= 38)
			{
				if ((message.compare(0, 38, "Your skill latency reduction value has") == 0 && m_isBSEnabled) ||
					(message.compare(0, 38, "In order to reach the next exploration") == 0 && m_isBECEnabled) ||
					(message.compare(0, 18, "Welcome to MabiPro") == 0 && m_isBWMEnabled))
				{
					// Hide spam
					PacketData data;
					data.type = 1;
					data.byte8 = 0;
					recvPacket.SetElement(&data, 0);
					BYTE* p;
					int tmpSizw = recvPacket.BuildPacket(&p);

					memcpy(mabiMessage.buffer, p, tmpSizw);
					free(p);
				}
			}
		}
		else if (m_isBOEEnabled && recvPacket.GetOP() == 0x526F)
		{
			if (strcmp(recvPacket.GetElement(0)->str, "You are over encumbered. Please clean out your Temporary Inventory.") == 0)
			{
				memset(mabiMessage.buffer, 0, mabiMessage.size);
			}
		}
	}
}