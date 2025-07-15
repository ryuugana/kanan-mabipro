#include "TickTimer.hpp"
#include "MabiPacket.h"
#include "imgui.h"
#include "Log.hpp"

namespace kanan {
	// Seconds between each tick
	unsigned int g_tickTimerSeconds;

	// 5 Mintues between ticks
	const unsigned int g_tickTimerMax = 300;

	TickTimer::TickTimer()
	{
		m_isEnabled = false;
		m_op.push_back(0x520E); // Tick sync packet; 0x5BD5 for durability update
		m_op.push_back(0x909A); // Nao count login packet

		g_tickTimerSeconds = 0;
		m_timerId = NULL;
		m_charId = 0;
	}

	void TickTimer::drawWindow() {
		ImGui::SetNextWindowSize(ImVec2{ 115.0f, 10.0f }, ImGuiCond_Appearing);

		if (!ImGui::Begin("TickTimer", &m_isEnabled, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoFocusOnAppearing)) {
			ImGui::End();
			return;
		}
		ImGui::Text("Next Tick: %d", g_tickTimerSeconds);
		ImGui::End();
	}

	void TickTimer::onUI() {
		if (ImGui::CollapsingHeader("Tick Timer")) {
			ImGui::TextWrapped("This mod shows the time until the next tick in seconds in a separate window.");
			ImGui::Dummy(ImVec2{ 5.0f, 5.0f });
			ImGui::TextWrapped("The window can be moved by dragging it to the desired location.");
			ImGui::Dummy(ImVec2{ 5.0f, 5.0f });
			ImGui::TextWrapped("Requires a relog after enabling.");
			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });

			ImGui::Checkbox("Enable Tick Timer", &m_isEnabled);
		}
	}

	bool TickTimer::onWindow() {
		if (m_isEnabled) {
			drawWindow();
		}

		return m_isEnabled;
	}

	void TickTimer::onConfigLoad(const Config& cfg) {
		m_isEnabled = cfg.get<bool>("TickTimer.Enabled").value_or(false);
	}

	void TickTimer::onConfigSave(Config& cfg) {
		cfg.set<bool>("TickTimer.Enabled", m_isEnabled);
	}

	VOID CALLBACK TickTimerProc(HWND hwnd, UINT message, UINT idTimer, DWORD dwTime)
	{
		if (g_tickTimerSeconds == 0 || g_tickTimerMax > g_tickTimerMax)
		{
			g_tickTimerSeconds = g_tickTimerMax;
		}
		else
		{
			g_tickTimerSeconds--;
		}
	}

	void TickTimer::onRecv(MabiMessage mabiMessage) {
		CMabiPacket recvPacket;
		recvPacket.SetSource(mabiMessage.buffer, mabiMessage.size);
		if (recvPacket.GetReciverId() < 0x10010000000000)
		{
			// Find out who we are on login
			// Move this to a global mod if our character ID is needed elsewhere
			if (recvPacket.GetOP() == 0x909A)
			{
				m_charId = recvPacket.GetReciverId();
			}

			// Create timer with 1 second interval 
			// Use TickTimerProc callback to update remaining seconds in window
			if (m_timerId == NULL)
			{
				m_timerId = SetTimer(NULL, m_timerId, 1000, TickTimerProc);
			}

			// Set max time for tick countdown if the packet is ours
			if (recvPacket.GetReciverId() == m_charId)
			{
				g_tickTimerSeconds = g_tickTimerMax;
			}
		}
	}
}