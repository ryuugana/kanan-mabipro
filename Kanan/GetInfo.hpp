#pragma once

#include "MessageMod.hpp"


namespace kanan {
	class GetInfo : public MessageMod {
	public:
		GetInfo();

		void onRecv(MabiMessage mabiMessage) override;

	private:
		void drawWindow();
	};
}