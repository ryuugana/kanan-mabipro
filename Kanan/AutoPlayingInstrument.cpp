#include "AutoPlayingInstrument.hpp"
#include "MabiPacket.h"
#include "imgui.h"
#include "Log.hpp"
#include "Kanan.hpp"

namespace kanan {
	AutoPlayingInstrument::AutoPlayingInstrument()
	{
		m_hasSend = false;
		m_hasRecv = true;
		m_isEnabled = false;
		m_op.push_back(0x6987); // Playing Instrument
		m_op.push_back(0x6988); // Playing Instrument
	}

	void AutoPlayingInstrument::onUI() {
		if (ImGui::TreeNode(getName().c_str())) {
			ImGui::TextWrapped("Track instrument playing events.");

			ImGui::Checkbox("Auto Playing Instrument", &m_isEnabled);
			ImGui::TreePop();
		}
	}

	void AutoPlayingInstrument::onConfigLoad(const Config& cfg) {
		m_isEnabled = cfg.get<bool>("AutoPlayingInstrument.Enabled").value_or(false);
	}

	void AutoPlayingInstrument::onConfigSave(Config& cfg) {
		cfg.set<bool>("AutoPlayingInstrument.Enabled", m_isEnabled);
	}

	// Player is sending an instrument playing event.
	void AutoPlayingInstrument::onRecv(MabiMessage mabiMessage) {
		if (!m_isEnabled) {
			return;
		}

		CMabiPacket packet;
		packet.SetSource(mabiMessage.buffer, mabiMessage.size);

		if(packet.GetReciverId() != g_kanan->characterId) {
			return;
        }

		if(packet.GetElementNum() < 1) {
			log("[AutoPlayingInstrument] A skill packet with %d elements, not the expected 1 - left to the player",
				packet.GetElementNum());
			return;
        }

		const PacketData* element = packet.GetElement(0);
		if (element->type != T_SHORT)
		{
			log("[AutoPlayingInstrument] A skill packet with element type %d, not the expected T_SHORT - left to the player",
				element->type);
			return;
		}

		if (element->word16 != 10003)
		{
			return;
		}

		CMabiPacket nextPlayPacket;
		nextPlayPacket.SetOP(0x6982);
        nextPlayPacket.SetReciverId(g_kanan->characterId);

        PacketData data;
        data.type = T_SHORT;
        data.word16 = 10003;
		nextPlayPacket.AddElement(&data);
		
		data.type = T_STRING;
		data.str = "";
        data.len = 0;
		nextPlayPacket.AddElement(&data);

		BYTE* p;
		int len;
		len = nextPlayPacket.BuildPacket(&p);

		if (len <= 0 || p == NULL) {
			return;
		}

		MabiMessage msg;
		msg.buffer = p;
		msg.size = len;
		AddToSendQ(msg);
	}
}
