#pragma once

#include "MessageMod.hpp"


namespace kanan {
	class ChooseLoginNode : public MessageMod {
	public:
		ChooseLoginNode();

		void onUI() override;

		void onConfigLoad(const Config& cfg) override;
		void onConfigSave(Config& cfg) override;

		static unsigned long onRecv(MabiMessage mabiMessage);
	private:
		static int m_choice;
	};
}