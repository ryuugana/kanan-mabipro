#pragma once

#include <Patch.hpp>

#include "PatchMod.hpp"

namespace kanan {
	// Automatically mute audio when Mabinogi is in the background
	class AutoMute : public PatchMod {
	public:
		AutoMute();

		void onPatchUI() override;

		void onConfigLoad(const Config& cfg) override;
		void onConfigSave(Config& cfg) override;

	private:
		std::optional<uintptr_t> address;
		bool m_choice;

		void apply();
	};
}
