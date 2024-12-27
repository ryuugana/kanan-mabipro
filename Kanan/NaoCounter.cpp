#include "NaoCounter.hpp"
#include "MabiPacket.h"
#include "imgui.h"
#include "Log.hpp"

namespace kanan {
	NaoCounter::NaoCounter()
	{
		m_count = 0;
		m_isEnabled = false;
		m_op.push_back(0x909A);
	}

	void NaoCounter::onUI() {
		if (ImGui::CollapsingHeader("Nao Counter")) {
			ImGui::TextWrapped("This mod allows you to see the nao revival counter in a separate window.\nRequires a relog on after enabling.\nCurrent Nao count: %d", m_count);

			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });

			ImGui::Checkbox("Enable Nao Counter", &m_isEnabled);
		}
	}

	void NaoCounter::onConfigLoad(const Config& cfg) {
		m_isEnabled = cfg.get<bool>("NaoCounter.Enabled").value_or(false);
	}

	void NaoCounter::onConfigSave(Config& cfg) {
		cfg.set<bool>("NaoCounter.Enabled", m_isEnabled);
	}

	void NaoCounter::onRecv(MabiMessage mabiMessage) {
		CMabiPacket recvPacket;
		recvPacket.SetSource(mabiMessage.buffer, mabiMessage.size);

		// Retrieve Nao Count
		m_count = m_maxNao - recvPacket.GetElement(1)->byte8;
	}
}