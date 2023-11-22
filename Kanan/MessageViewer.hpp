#pragma once

#include "MessageMod.hpp"


namespace kanan {
	class MessageViewer : public MessageMod {
	public:
		MessageViewer();

		void onUI() override;

		void onConfigLoad(const Config& cfg) override;
		void onConfigSave(Config& cfg) override;

		void onRecv(MabiMessage mabiMessage) override;
	};
}