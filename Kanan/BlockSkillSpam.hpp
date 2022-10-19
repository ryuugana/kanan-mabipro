#pragma once

#include "MessageMod.hpp"


namespace kanan {
	class BlockSkillSpam : public MessageMod {
	public:
		BlockSkillSpam();

		void onUI() override;

		void onConfigLoad(const Config& cfg) override;
		void onConfigSave(Config& cfg) override;

		static void onRecv(MabiMessage mabiMessage);
	};
}