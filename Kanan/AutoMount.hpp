#pragma once

#include "MessageMod.hpp"


namespace kanan {
	class AutoMount : public MessageMod {
	public:
		AutoMount();

		std::string getName() override { return "Auto Accept Mount Requests"; }

		void onUI() override;

		void onConfigLoad(const Config& cfg) override;
		void onConfigSave(Config& cfg) override;

		void onRecv(MabiMessage mabiMessage) override;
	};
}