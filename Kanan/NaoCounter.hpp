#pragma once

#include "MessageMod.hpp"


namespace kanan {
	class NaoCounter : public MessageMod {
	public:
		NaoCounter();

		void onUI() override;

		bool onWindow() override;

		void onConfigLoad(const Config& cfg) override;
		void onConfigSave(Config& cfg) override;

		void onRecv(MabiMessage mabiMessage) override;

	private:
		void drawWindow();

		unsigned int m_count;

		// Nao's Support: 5
		const unsigned int m_maxNao = 5;
	};
}