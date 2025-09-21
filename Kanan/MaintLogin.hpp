#pragma once

#include "MessageMod.hpp"


namespace kanan {
	class MaintLogin : public MessageMod {
	public:
		MaintLogin();

		void onUI() override;

		void onConfigLoad(const Config& cfg) override;
		void onConfigSave(Config& cfg) override;

		void onRecv(MabiMessage mabiMessage) override;
	};
}