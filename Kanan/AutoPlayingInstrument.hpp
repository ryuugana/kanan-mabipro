#pragma once

#include "MessageMod.hpp"


namespace kanan {
	class AutoPlayingInstrument : public MessageMod {
	public:
		AutoPlayingInstrument();

		std::string getName() override { return "Auto Playing Instrument"; }

		void onUI() override;

		void onConfigLoad(const Config& cfg) override;
		void onConfigSave(Config& cfg) override;

		void onRecv(MabiMessage mabiMessage) override;
	};
}
