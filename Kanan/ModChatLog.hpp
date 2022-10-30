#pragma once

#include "MessageMod.hpp"


namespace kanan {
	class ModChatLog : public MessageMod {
	public:
		ModChatLog();

		void onUI() override;

		void onConfigLoad(const Config& cfg) override;
		void onConfigSave(Config& cfg) override;

		unsigned long onRecv(MabiMessage mabiMessage) override;
	private:
		void startLogging();

		bool m_fileLogEnabled;
		bool m_startedLogging;
	};
}