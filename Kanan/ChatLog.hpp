#pragma once

#include "MessageMod.hpp"


namespace kanan {
	class ChatLog : public MessageMod {
	public:
		ChatLog();

		void onUI() override;

		void onConfigLoad(const Config& cfg) override;
		void onConfigSave(Config& cfg) override;

		static unsigned long onRecv(MabiMessage mabiMessage);
	};
}