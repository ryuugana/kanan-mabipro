#pragma once

#include <chrono>
#include <vector>

#include "MessageMod.hpp"


namespace kanan {
	class DpsMeter : public MessageMod {
	public:
		DpsMeter();

		std::string getName() override { return "Dps Meter"; }

		void onUI() override;

		bool onWindow() override;

		void onConfigLoad(const Config& cfg) override;
		void onConfigSave(Config& cfg) override;

		void onRecv(MabiMessage mabiMessage) override;

	private:
		void drawWindow();

		std::chrono::time_point<std::chrono::steady_clock> m_startTime;
		std::chrono::time_point<std::chrono::steady_clock> m_lastTime;
		double m_dps; // accumulated damage (used as double so fractional values are allowed)
		int m_timeout;

		// Samples for plotting DPS history
		std::vector<float> m_samples;
		int m_sampleCount;
		int m_nextSampleIndex;
		double m_sampleInterval; // seconds between samples
		std::chrono::time_point<std::chrono::steady_clock> m_lastSampleTime;
	};
}