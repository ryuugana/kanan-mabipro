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
			ImGui::Checkbox(getName().c_str(), &m_isEnabled);
	}

	void AutoMount::onConfigLoad(const Config& cfg) {
		m_isEnabled = cfg.get<bool>("AutoMount.Enabled").value_or(false);
	}

	void AutoMount::onConfigSave(Config& cfg) {
		cfg.set<bool>("AutoMount.Enabled", m_isEnabled);
	}

	// Another player asked to ride with us. The server sends the question; we answer it ourselves
	// instead of showing the player the yes/no box. The answer is a copy of the question, marked as
	// an answer (0x1FBD6) with its third value, the reply, set to 1 for "yes".
	void AutoMount::onRecv(MabiMessage mabiMessage) {
		CMabiPacket mountPacket;
		mountPacket.SetSource(mabiMessage.buffer, mabiMessage.size);

		// A question in a form we don't know (fewer values) is left for the player to answer.
		if (mountPacket.GetElementNum() < 3) {
			log("[AutoMount] A mount request with %d elements, not the 3 or more expected - left to the player",
				mountPacket.GetElementNum());
			return;
		}

		// Turn the question into the answer: "yes".
		mountPacket.SetOP(0x1FBD6);

		PacketData data;
		data.type = T_BYTE;
		data.byte8 = 1;
		mountPacket.SetElement(&data, 2);

		BYTE* p;
		int len;
		len = mountPacket.BuildPacket(&p);

		if (len <= 0 || p == NULL) {
			return;
		}

		// Kanan sends it as soon as the mods have seen this message.
		MabiMessage msg;
		msg.buffer = p;
		msg.size = len;
		AddToSendQ(msg);

		// Blank the question, so the game never shows the player the yes/no box.
		memset(mabiMessage.buffer, 0, mabiMessage.size);
	}
}