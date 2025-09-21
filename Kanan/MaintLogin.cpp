#include "MaintLogin.hpp"
#include "MabiPacket.h"
#include "imgui.h"
#include "Log.hpp"

namespace kanan {

	MaintLogin::MaintLogin()
	{
		m_isEnabled = false;
		m_op.push_back(0x23);
		m_op.push_back(0x26);
	}

	void MaintLogin::onUI() {
		if (ImGui::CollapsingHeader("Maintenance Login")) {
			ImGui::TextWrapped("This mod ignores the server status when logging in.\n"
				"This will allow you to login during channel maintenance.\n\n"
				"Note: Only works with GM accounts, mainly used for development.");


			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });

			ImGui::Checkbox("Enabled", &m_isEnabled);
		}
	}

	void MaintLogin::onConfigLoad(const Config& cfg) {
		m_isEnabled = cfg.get<bool>("MaintLogin.Enabled").value_or(false);
	}

	void MaintLogin::onConfigSave(Config& cfg) {
		cfg.set<bool>("MaintLogin.Enabled", m_isEnabled);
	}

	void MaintLogin::onRecv(MabiMessage mabiMessage) {
		CMabiPacket recvPacket;
		recvPacket.SetSource(mabiMessage.buffer, mabiMessage.size);

		// Change status to normal
		PacketData data;
		data.type = T_INT;
		data.int32 = 1;
		if(recvPacket.GetOP() == 0x23)
			recvPacket.SetElement(&data, 11);
		else
			recvPacket.SetElement(&data, 7);

		BYTE* p;
		int tmpSizw = recvPacket.BuildPacket(&p);

		memcpy(mabiMessage.buffer, p, tmpSizw);
		free(p);
	}
}