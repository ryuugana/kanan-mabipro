#include <imgui.h>
#include <chrono>

#include "Log.hpp"
#include "MaxFrameRate.hpp"

namespace kanan {
	// A timer precise to well under a millisecond (Windows 10 1803 and later).
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
	constexpr DWORD CREATE_WAITABLE_TIMER_HIGH_RESOLUTION = 0x2;
#endif

	MaxFrameRate::MaxFrameRate()
		: m_maxFPS{ 0 },
		m_maxBackgroundFPS{ 0 },
		m_enabled{ false },
		m_nextFrame{},
		m_timer{ nullptr },
		m_isTimerPrecise{ true }
	{
		m_timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);

		// Older Windows: a normal timer, which can wake a millisecond or two late.
		if (m_timer == nullptr) {
			m_isTimerPrecise = false;
			m_timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
		}

		log("[MaxFrameRate] Waiting between frames with %s", m_timer == nullptr ? "Sleep (no timer)" :
			m_isTimerPrecise ? "a high resolution timer" : "a normal timer");
	}

	MaxFrameRate::~MaxFrameRate()
	{
		if (m_timer != nullptr) {
			CloseHandle(m_timer);
		}
	}

	// Waits until TIME, resting the CPU; only the last moment is waited out by checking the clock,
	// in case the timer wakes early.
	void MaxFrameRate::waitUntil(std::chrono::steady_clock::time_point time) {
		using namespace std::chrono;

		auto margin = m_isTimerPrecise ? microseconds{ 200 } : milliseconds{ 2 };
		auto remaining = time - steady_clock::now();

		if (remaining > margin) {
			// Relative due times are negative, in 100 nanosecond units.
			LARGE_INTEGER due{};
			due.QuadPart = -duration_cast<duration<LONGLONG, std::ratio<1, 10000000>>>(remaining - margin).count();

			if (m_timer != nullptr && SetWaitableTimer(m_timer, &due, 0, nullptr, nullptr, FALSE)) {
				WaitForSingleObject(m_timer, INFINITE);
			}
			else {
				while (time - steady_clock::now() > margin) {
					Sleep(1);
				}
			}
		}

		while (steady_clock::now() < time) {
			YieldProcessor();
		}
	}

	void MaxFrameRate::onFrameDrawn() {
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
			m_nextFrame = {};
			return;
		}

		using namespace std::chrono;

		auto frameTime = duration_cast<steady_clock::duration>(duration<double>{ 1.0 / maxFPS });
		auto now = steady_clock::now();

		// Start the schedule over when it's new, or more than a frame behind (a slow frame, or the
		// frame rate changing), rather than rushing frames to catch up.
		if (m_nextFrame == steady_clock::time_point{} || now > m_nextFrame + frameTime)
		{
			m_nextFrame = now;
		}

		waitUntil(m_nextFrame);

		m_nextFrame += frameTime;
	}

	//-----------------------------------------------------------------------------
	// UI Functions

	void MaxFrameRate::onUI() {
		if (ImGui::TreeNode("Max Frame Rate")) {
			ImGui::TextWrapped("Max Frame Rate");
			ImGui::InputInt("FPS", &m_maxFPS, 1, 10);
			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
			ImGui::TextWrapped("Background Max Frame Rate");
			ImGui::InputInt("Background FPS", &m_maxBackgroundFPS, 1, 10);
			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
			ImGui::TextWrapped("Set the max frame rate for Mabinogi. Set to 0 to disable.");
			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
			ImGui::TextWrapped("Background frame rate will limit the frame rate of Mabinogi while another window is focused.");
			ImGui::TreePop();
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