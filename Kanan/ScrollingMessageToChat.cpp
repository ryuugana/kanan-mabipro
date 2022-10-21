#include "ScrollingMessageToChat.hpp"
#include "MabiPacket.h"
#include "imgui.h"
#include "Log.hpp"

namespace kanan {
	ScrollingMessageToChat::ScrollingMessageToChat()
	{
		m_funcPtr = onRecv;
		m_isEnabled = false;
		m_op = 21101;
	}

	void ScrollingMessageToChat::onUI() {
		if (ImGui::CollapsingHeader("Scrolling Message To Chat")) {
			ImGui::TextWrapped("This mod moves scrolling messages from the top of the screen to the middle of the screen and chat as <System> messages.");
			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
			ImGui::Checkbox("Enable Scrolling Message To Chat", &m_isEnabled);
		}
	}

	void ScrollingMessageToChat::onConfigLoad(const Config& cfg) {
		m_isEnabled = cfg.get<bool>("ScrollingMessageToChat.Enabled").value_or(false);
	}

	void ScrollingMessageToChat::onConfigSave(Config& cfg) {
		cfg.set<bool>("ScrollingMessageToChat.Enabled", m_isEnabled);
	}

	unsigned long ScrollingMessageToChat::onRecv(MabiMessage mabiMessage) {
		CMabiPacket recvPacket;
		recvPacket.SetSource(mabiMessage.buffer, mabiMessage.size);

		if (recvPacket.GetElement(0)->byte8 == 7 || recvPacket.GetElement(0)->byte8 == 4 || recvPacket.GetElement(0)->byte8 == 10)
			return recvPacket.GetOP();

		PacketData data;
		data.type = 1;
		data.byte8 = 7;
		recvPacket.SetElement(&data, 0);
			BYTE* p;
			int tmpSizw = recvPacket.BuildPacket(&p);

			memcpy(mabiMessage.buffer, p, tmpSizw);

		return recvPacket.GetOP();
	}
}