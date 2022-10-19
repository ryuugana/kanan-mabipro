#pragma once

#include "MessageMod.hpp"


namespace kanan {
	class ScrollingMessageToChat : public MessageMod {
	public:
		ScrollingMessageToChat();

		void onUI() override;

		void onConfigLoad(const Config& cfg) override;
		void onConfigSave(Config& cfg) override;

		static unsigned long onRecv(MabiMessage mabiMessage);
	};
}