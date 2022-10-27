#pragma once

#include "MessageMod.hpp"


namespace kanan {
	class MessageViewer : public MessageMod {
	public:
		MessageViewer();

		void onUI() override;

		void onConfigLoad(const Config& cfg) override;
		void onConfigSave(Config& cfg) override;

		unsigned long onRecv(MabiMessage mabiMessage) override;
	};
}