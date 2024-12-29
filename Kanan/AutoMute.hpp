#pragma once

#include <Patch.hpp>

#include "PatchMod.hpp"

namespace kanan {
	// Disable flashy effects and fix AstralWorlds broken disable by showing the non-flashy varient
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
