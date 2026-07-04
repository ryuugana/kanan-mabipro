#pragma once

#include <vector>
#include "MessageMod.hpp"


namespace kanan {
	class AutoLoginChannel : public MessageMod {
	public:
		AutoLoginChannel();

		void onUI() override;

		void onConfigLoad(const Config& cfg) override;
		void onConfigSave(Config& cfg) override;

		void onRecv(MabiMessage mabiMessage) override;
	private:
		static int m_choice;
		std::vector<char*> m_channels;
	};
}