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
		m_op.push_back(0x526d);
	}

	void NaoCounter::drawWindow() {
		ImGui::SetNextWindowSize(ImVec2{ 0.0f, 10.0f }, ImGuiCond_Appearing);

		if (!ImGui::Begin("", &m_isEnabled, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoFocusOnAppearing)) {
			ImGui::End();
			return;
		}
		ImGui::Text("Nao's Support: %d", m_count);
		ImGui::End();
	}

	void NaoCounter::onUI() {
		if (ImGui::CollapsingHeader("Nao Counter")) {
			ImGui::TextWrapped("This mod allows you to see the nao revival counter in a separate window.");
			ImGui::Dummy(ImVec2{ 5.0f, 5.0f });
			ImGui::TextWrapped("Designed to be used with AstralWorld's timer mod, which breaks viewing Nao's Support from the clock.");
			ImGui::Dummy(ImVec2{ 5.0f, 5.0f });
			ImGui::TextWrapped("The window can be moved by dragging it to the desired location.");
			ImGui::Dummy(ImVec2{ 5.0f, 5.0f });
			ImGui::TextWrapped("Requires a relog after enabling.");
			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });

			ImGui::Checkbox("Enable Nao Counter", &m_isEnabled);
		}
	}

	void NaoCounter::onWindow() {
		if (m_isEnabled) {
			drawWindow();
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
		if (recvPacket.GetOP() == 0x909A)
		{
			// Retrieve Nao Count
			m_count = m_maxNao - recvPacket.GetElement(1)->byte8;
		}
		else
		{
			if (recvPacket.GetReciverId() == 0x3000000000000000)
			{
				string message = recvPacket.GetElement(1)->str;
				if (message.find("Eweca has set.") != std::string::npos)
				{
					if (m_count < m_maxNao)
					{
						m_count++;
					}
				}
			}
		}
	}
}