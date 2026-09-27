#pragma once

#include "MessageMod.hpp"


namespace kanan {
	class TickTimer : public MessageMod {
	public:
		TickTimer();

		std::string getName() override { return "Tick Timer"; }

		void onUI() override;

		bool onWindow() override;

		void onConfigLoad(const Config& cfg) override;
		void onConfigSave(Config& cfg) override;

		void onRecv(MabiMessage mabiMessage) override;

	private:
		void drawWindow();

		UINT_PTR m_timerId;
	};
}