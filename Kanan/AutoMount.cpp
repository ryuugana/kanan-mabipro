#include "AutoMount.hpp"
#include "MabiPacket.h"
#include "imgui.h"
#include "Log.hpp"

namespace kanan {
	AutoMount::AutoMount()
	{
		m_hasSend = false;
		m_hasRecv = true;
		m_isEnabled = false;
		m_op.push_back(0x1FBD5); // Mount Request
	}

	void AutoMount::onUI() {
		if (ImGui::TreeNode(getName().c_str())) {
			ImGui::TextWrapped("Accept mount requests automatically.");

			ImGui::Checkbox("Enable Auto Accept Mount Request", &m_isEnabled);
			ImGui::TreePop();
		}
	}

	void AutoMount::onConfigLoad(const Config& cfg) {
		m_isEnabled = cfg.get<bool>("AutoMount.Enabled").value_or(false);
	}

	void AutoMount::onConfigSave(Config& cfg) {
		cfg.set<bool>("AutoMount.Enabled", m_isEnabled);
	}

	void AutoMount::onRecv(MabiMessage mabiMessage) {
		CMabiPacket mountPacket;
		mountPacket.SetSource(mabiMessage.buffer, mabiMessage.size);

		mountPacket.SetOP(0x1FBD6);

		PacketData data;
		data.type = T_BYTE;
		data.byte8 = 1;
		mountPacket.SetElement(&data, 2);

		BYTE* p;
		int len;
		len = mountPacket.BuildPacket(&p);

		MabiMessage msg;
		msg.buffer = p;
		msg.size = len;
		AddToSendQ(msg);

		// Delete original message to prevent request box from showing
		memset(mabiMessage.buffer, 0, mabiMessage.size);
	}
}