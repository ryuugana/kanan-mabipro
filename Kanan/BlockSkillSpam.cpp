#include "BlockSkillSpam.hpp"
#include "MabiPacket.h"
#include "imgui.h"
#include "Log.hpp"

namespace kanan {
	BlockSkillSpam::BlockSkillSpam()
	{
		m_funcPtr = onRecv;
		m_isEnabled = false;
		m_op = 21101;
	}

	void BlockSkillSpam::onUI() {
		if (ImGui::CollapsingHeader("Block >skill Spam")) {
			ImGui::Text("This mod blocks >skill from spamming the following message:");
			ImGui::Text("\"Your skill latency reduction value has been detected to be too high. Please lower it..\"");
			ImGui::Text("This mod was made because usually >skill will spam you wether you use it correctly or not");
			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
			ImGui::Text(">skill is a command part of MabiPro that slightly decreases skill load time");
			ImGui::Text(">skill is designed to be used with your ping to the node.");
			ImGui::Text("Example: type >skill 102 if your ping to vier.mabi.pro is 102 and you are on Vier");
			ImGui::Text(">server will tell you which node you are on");
			ImGui::Text(">skill 0 or lower will disable the command");
			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
			ImGui::Checkbox("Enable Block >skill Spam", &m_isEnabled);
		}
	}

	void BlockSkillSpam::onConfigLoad(const Config& cfg) {
		m_isEnabled = cfg.get<bool>("BlockSkillSpam.Enabled").value_or(false);
	}

	void BlockSkillSpam::onConfigSave(Config& cfg) {
		cfg.set<bool>("BlockSkillSpam.Enabled", m_isEnabled);
	}

	unsigned long BlockSkillSpam::onRecv(MabiMessage mabiMessage) {
		CMabiPacket recvPacket;
		recvPacket.SetSource(mabiMessage.buffer, mabiMessage.size);

		if (strcmp(recvPacket.GetElement(1)->str, "Your skill latency reduction value has been detected to be too high. Please lower it..") == 0) {
			/* Alternative
			// Hide >skill spam
			PacketData data;
			data.type = 1;
			data.byte8 = 0;
			recvPacket.SetElement(&data, 0);
			BYTE* p;
			int tmpSizw = recvPacket.BuildPacket(&p);

			memcpy(mabiMessage.buffer, p, tmpSizw);
			*/

			// Remove >skill spam
			memset(mabiMessage.buffer, 0, mabiMessage.size);
			return 0;
		}

		return recvPacket.GetOP();
	}
}