#include "DpsMeter.hpp"
#include "MabiPacket.h"
#include "imgui.h"
#include "Log.hpp"
#include "Kanan.hpp"
#include <algorithm>
#include <limits>
#include <vector>

namespace kanan {
	DpsMeter::DpsMeter()
	{
		m_hasSend = false;
		m_hasRecv = true;
		m_isEnabled = false;
		m_op.push_back(0x7924); // Dmg dealt
		m_timeout = 10;

		m_dps = 0.0;
		m_sampleCount = 80;
		m_sampleInterval = 0.5; // seconds
		m_samples.assign(m_sampleCount, 0.0f);
		m_nextSampleIndex = 0;
		m_lastSampleTime = std::chrono::steady_clock::now();
	}

	void DpsMeter::drawWindow() {
		ImGui::SetNextWindowSize(ImVec2{ 120.0f, 80.0f }, ImGuiCond_FirstUseEver);

		if (!ImGui::Begin("DpsMeter", &m_isEnabled, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing)) {
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
				// reset samples
				std::fill(m_samples.begin(), m_samples.end(), 0.0f);
				m_nextSampleIndex = 0;
				m_lastSampleTime = std::chrono::steady_clock::now();
			}
			else
			{
				std::chrono::duration<double> elapsed_seconds = std::chrono::steady_clock::now() - m_startTime;
				dps = m_dps / elapsed_seconds.count();
			}
		}

		// sampling
		auto now = std::chrono::steady_clock::now();
		double sinceLastSample = std::chrono::duration<double>(now - m_lastSampleTime).count();
		if (sinceLastSample >= m_sampleInterval) {
			m_samples[m_nextSampleIndex] = static_cast<float>(dps);
			m_nextSampleIndex = (m_nextSampleIndex + 1) % m_sampleCount;
			m_lastSampleTime = now;
		}

		// compute min/max
		float minv = std::numeric_limits<float>::max();
		float maxv = std::numeric_limits<float>::lowest();
		for (float v : m_samples) {
			if (v < minv) minv = v;
			if (v > maxv) maxv = v;
		}
		if (minv == std::numeric_limits<float>::max()) minv = 0.0f;
		if (maxv == std::numeric_limits<float>::lowest()) maxv = 0.0f;

		float margin = (maxv - minv) * 0.1f;
		if (margin == 0.0f) {
			minv = 0.0f;
			maxv = maxv + 1.0f;
		} else {
			minv = std::max(0.0f, minv - margin);
			maxv = maxv + margin;
		}

		char overlay[64];
		std::snprintf(overlay, sizeof(overlay), "DPS: %.0f", dps);

		// linearize circular buffer
		std::vector<float> linear;
		linear.reserve(m_sampleCount);
		for (int i = 0; i < m_sampleCount; ++i) {
			int idx = (m_nextSampleIndex + i) % m_sampleCount;
			linear.push_back(m_samples[idx]);
		}

		// make the plot fill the available content region in the window
		ImVec2 avail = ImGui::GetContentRegionAvail();
		ImVec2 plotSize = avail;
		// sensible minimums/fallbacks
		if (plotSize.x <= 0.0f) plotSize.x = 200.0f;
		if (plotSize.y < ImGui::GetFontSize() * 3.0f) plotSize.y = ImGui::GetFontSize() * 3.0f;

		ImGui::PlotLines("##DpsPlot", linear.data(), m_sampleCount, 0, overlay, minv, maxv, plotSize);


		ImGui::End();
	}

	void DpsMeter::onUI() {
		if (ImGui::TreeNode(getName().c_str())) {
			ImGui::TextWrapped("This mod displays a DPS meter in a separate window.");
			ImGui::Dummy(ImVec2{ 5.0f, 5.0f });
			ImGui::TextWrapped("Timeout is the amount of time spent not attacking in seconds before the DPS resets.");
			ImGui::Dummy(ImVec2{ 5.0f, 5.0f });
			ImGui::TextWrapped("The window can be moved by dragging it to the desired location.");
			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });

			ImGui::Checkbox("Enable DPS Meter", &m_isEnabled);
			ImGui::InputInt("Timeout", &m_timeout);
			ImGui::Dummy(ImVec2{ 5.0f, 5.0f });
			ImGui::TreePop();
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