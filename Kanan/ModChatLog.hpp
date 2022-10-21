#pragma once

#include "MessageMod.hpp"


namespace kanan {
	class ModChatLog : public MessageMod {
	public:
		ModChatLog();

		void onUI() override;

		void onConfigLoad(const Config& cfg) override;
		void onConfigSave(Config& cfg) override;

		static unsigned long onRecv(MabiMessage mabiMessage);
	private:
		static bool m_fileLogEnabled;
	};
}