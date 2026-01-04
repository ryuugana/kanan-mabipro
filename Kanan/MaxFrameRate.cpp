#include <imgui.h>
#include <chrono>

#include "Log.hpp"
#include "MaxFrameRate.hpp"

namespace kanan {
	MaxFrameRate::MaxFrameRate()
	{
		m_elapsedTime = std::chrono::system_clock::now();
	}

	void MaxFrameRate::onFrame() {
		if (!m_enabled)
		{
			return;
		}

		int maxFPS = 0;

		if (GetActiveWindow() != GetForegroundWindow() && m_maxBackgroundFPS > 0)
		{
			maxFPS = m_maxBackgroundFPS;
		}
		else
		{
			maxFPS = m_maxFPS;
		}

		if (maxFPS <= 0)
		{
			return;
		}

		float remaining = 0.f;
		float frameTime = 1.f / maxFPS;

		// Sleep formula may need to be changed
		// Windows does not provide reliable sleep at less than 5ms
		do
		{
			remaining = std::chrono::duration<float>(std::chrono::system_clock::now() - m_elapsedTime).count();

			if (frameTime - remaining > 0.0034f)
			{
				Sleep(1);
			}

		} while (remaining < frameTime);

		m_elapsedTime = std::chrono::system_clock::now();
	}

	//-----------------------------------------------------------------------------
	// UI Functions

	void MaxFrameRate::onUI() {
		if (ImGui::CollapsingHeader("Max Frame Rate")) {
			ImGui::TextWrapped("Max Frame Rate");
			ImGui::InputInt("FPS", &m_maxFPS, 1, 10);
			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
			ImGui::TextWrapped("Background Max Frame Rate");
			ImGui::InputInt("Background FPS", &m_maxBackgroundFPS, 1, 10);
			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
			ImGui::TextWrapped("Set the max frame rate for Mabinogi. Set to 0 to disable.");
			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
			ImGui::TextWrapped("Background frame rate will limit the frame rate of Mabinogi while another window is focused.");
		}

		if (m_maxFPS || m_maxBackgroundFPS)
		{
			m_enabled = true;
		}
	}

	void MaxFrameRate::onConfigLoad(const Config& cfg) {
		m_maxFPS = cfg.get<int>("UI.MaxFrameRate").value_or(0);
		m_maxBackgroundFPS = cfg.get<int>("UI.BackgroundMaxFrameRate").value_or(0);

		if (m_maxFPS || m_maxBackgroundFPS)
		{
			m_enabled = true;
		}
	}

	void MaxFrameRate::onConfigSave(Config& cfg) {
		cfg.set<int>("UI.MaxFrameRate", m_maxFPS);
		cfg.set<int>("UI.BackgroundMaxFrameRate", m_maxBackgroundFPS);
	}
}