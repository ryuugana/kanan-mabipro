#pragma once

#include <chrono>

#include "MessageMod.hpp"


namespace kanan {
	class DpsMeter : public MessageMod {
	public:
		DpsMeter();

		void onUI() override;

		bool onWindow() override;

		void onConfigLoad(const Config& cfg) override;
		void onConfigSave(Config& cfg) override;

		void onRecv(MabiMessage mabiMessage) override;

	private:
		void drawWindow();

		std::chrono::time_point<std::chrono::steady_clock> m_startTime;
		std::chrono::time_point<std::chrono::steady_clock> m_lastTime;
		UINT64 m_dps;
		int m_timeout;
	};
}