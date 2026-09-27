#pragma once

#include <chrono>

#include "Mod.hpp"

namespace kanan {
	// Limit frame rate of Mabinogi
	class MaxFrameRate : public Mod {
	public:
		MaxFrameRate();
		~MaxFrameRate();

		// Waits for the next frame last, just before the game shows it, so the frames stay evenly spaced.
		void onFrameDrawn() override;

		void onUI() override;

		void onConfigLoad(const Config& cfg) override;
		void onConfigSave(Config& cfg) override;

	private:
		int m_maxFPS;
		int m_maxBackgroundFPS;
		bool m_enabled;

		// When the next frame is due; frames are scheduled a frame time apart, so waits don't drift.
		std::chrono::steady_clock::time_point m_nextFrame;
		// Waits without using the CPU (null if Windows can't make one).
		HANDLE m_timer;
		bool m_isTimerPrecise;

		void waitUntil(std::chrono::steady_clock::time_point time);
	};
}
