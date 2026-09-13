#pragma once

#include "MessageMod.hpp"


namespace kanan {
	class ScrollingMessageToChat : public MessageMod {
	public:
		ScrollingMessageToChat();

		std::string getName() override { return "Scrolling Message To Chat"; }

		void onUI() override;

		void onConfigLoad(const Config& cfg) override;
		void onConfigSave(Config& cfg) override;

		void onRecv(MabiMessage mabiMessage) override;

	private:
		bool m_isAuctionEnabled;
		bool m_isFieldBossEnabled;
		bool m_isFieldBNotifyEnabled;
	};
}