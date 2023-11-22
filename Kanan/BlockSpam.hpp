#pragma once

#include "MessageMod.hpp"


namespace kanan {
	class BlockSpam : public MessageMod {
	public:
		BlockSpam();

		void onUI() override;

		void onConfigLoad(const Config& cfg) override;
		void onConfigSave(Config& cfg) override;

		void onRecv(MabiMessage mabiMessage) override;

	private:
		bool m_isBSEnabled;
		bool m_isBOEEnabled;
	};
}