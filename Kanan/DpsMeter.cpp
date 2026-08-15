#include "DpsMeter.hpp"
#include "MabiPacket.h"
#include "imgui.h"
#include "Log.hpp"
#include "Kanan.hpp"

namespace kanan {
	DpsMeter::DpsMeter()
	{
		m_hasSend = false;
		m_hasRecv = true;
		m_isEnabled = false;
		m_op.push_back(0x7924); // Dmg dealt
		m_timeout = 10;
	}

	void DpsMeter::drawWindow() {
		ImGui::SetNextWindowSize(ImVec2{ ImGui::GetFontSize() * 8.0f, ImGui::GetFontSize() * 1.7f + 5.0f }, ImGuiCond_Appearing);

		if (!ImGui::Begin("DpsMeter", &m_isEnabled, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoFocusOnAppearing)) {
			ImGui::End();
			return;
		}

		double dps = 0;

		if (m_startTime.time_since_epoch() != std::chrono::steady_clock::duration::zero()) 
		{
			std::chrono::duration<double> elapsed_seconds = std::chrono::steady_clock::now() - m_lastTime;
			if (elapsed_seconds.count() > m_timeout)
			{
				m_startTime = {};
				m_dps = 0;
			}
			else
			{
				std::chrono::duration<double> elapsed_seconds = std::chrono::steady_clock::now() - m_startTime;
				dps = m_dps / elapsed_seconds.count();
			}
		}

		ImGui::TextWrapped("DPS: %.0f", dps);
		ImGui::End();
	}

	void DpsMeter::onUI() {
		if (ImGui::CollapsingHeader("DPS Meter")) {
			ImGui::TextWrapped("This mod displays a DPS meter in a separate window.");
			ImGui::Dummy(ImVec2{ 5.0f, 5.0f });
			ImGui::TextWrapped("Timeout is the amount of time spent not attacking in seconds before the DPS resets.");
			ImGui::Dummy(ImVec2{ 5.0f, 5.0f });
			ImGui::TextWrapped("The window can be moved by dragging it to the desired location.");
			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });

			ImGui::Checkbox("Enable DPS Meter", &m_isEnabled);
			ImGui::InputInt("Timeout", &m_timeout);
			ImGui::Dummy(ImVec2{ 5.0f, 5.0f });
		}
	}

	bool DpsMeter::onWindow() {
		if (m_isEnabled) {
			drawWindow();
		}

		return m_isEnabled;
	}

	void DpsMeter::onConfigLoad(const Config& cfg) {
		m_isEnabled = cfg.get<bool>("DpsMeter.Enabled").value_or(false);
		m_timeout = cfg.get<int>("DpsMeter.Timeout").value_or(10);
	}

	void DpsMeter::onConfigSave(Config& cfg) {
		cfg.set<bool>("DpsMeter.Enabled", m_isEnabled);
		cfg.set<int>("DpsMeter.Timeout", m_timeout);
	}

	void DpsMeter::onRecv(MabiMessage mabiMessage) {
		CMabiPacket recvPacket;
		recvPacket.SetSource(mabiMessage.buffer, mabiMessage.size);
		int numElements = recvPacket.GetElementNum();

		if (numElements > 0 && recvPacket.GetElement(numElements - 1)->ID == g_kanan->characterId)
		{
			if (m_startTime.time_since_epoch() == std::chrono::steady_clock::duration::zero())
			{
				m_startTime = std::chrono::steady_clock::now();
				m_lastTime = m_startTime;
			}
			else
			{
				m_lastTime = std::chrono::steady_clock::now();
			}
			m_dps += recvPacket.GetElement(7)->float32;
		}
	}
}