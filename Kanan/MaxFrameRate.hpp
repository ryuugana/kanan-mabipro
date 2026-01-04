#pragma once

#include <chrono>

#include "Mod.hpp"

namespace kanan {
	// Limit frame rate of Mabinogi
	class MaxFrameRate : public Mod {
	public:
		MaxFrameRate();

		void onFrame() override;

		void onUI() override;

		void onConfigLoad(const Config& cfg) override;
		void onConfigSave(Config& cfg) override;

	private:
		int m_maxFPS;
		int m_maxBackgroundFPS;
		bool m_enabled;

		std::chrono::system_clock::time_point m_elapsedTime;
	};
}
