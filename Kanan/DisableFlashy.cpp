#include <Scan.hpp>

#include "Log.hpp"
#include <imgui.h>
#include "DisableFlashy.h"

namespace kanan {

	LPBYTE addrTargetReturn = NULL;
	int tempAddr = 0;
	unsigned int hexColorCode = 0;
	int tempDiff = 0;

	DisableFlashy::DisableFlashy()
	{
		// Disable Flashy for items in inventory and on ground
		address = scan("Standard.dll", "8B 44 24 04 8B 44 81 10 C2 04");

		if (address) {
			log("Got Disable Flashy %p", *address);
		}
		else {
			log("Failed to find Disable Flashy address.");
		}

		// AstralWorld AntiFlashy Fix
		auto fix = scan("mss32.dll", "81 E9 00 00 00 01");

		if (fix) {
			log("Got Disable Flashy Fix %p", *fix);
			m_patch.address = *fix;
			m_patch.bytes = { 0x81, 0xC1, 0x00, 0x00, 0x00, 0x10 };
		}
		else {
			log("Failed to find Disable Flashy Fix address.");
		}
	}

	//-----------------------------------------------------------------------------
	// Hook functions

	__declspec(naked) void patchFlashyValue()
	{
		__asm {
			mov     eax, [esp + 04]
			mov     eax, [ecx + eax * 4 + 10h]
			mov		hexColorCode, eax
		}

		if (hexColorCode >= 0x40000000 && hexColorCode <= 0x7F000000) {
			tempDiff = hexColorCode >> 24;
			tempDiff = tempDiff << 24;
			hexColorCode -= tempDiff;
			hexColorCode += 0x10000000;
		}

		__asm {
			mov		eax, hexColorCode
			ret		0004
		}
	}

	void DisableFlashy::apply() {
		if (m_choice) {
			Hookjmp((void*)(*address), patchFlashyValue, 8);
			if (m_patch.address != 0) {
				patch(m_patch);
			}
		}
		else {
            Patchmem((BYTE*)(*address), (BYTE*)"\x8B\x44\x24\x04\x8B\x44\x81\x10", 8);

			if (m_patch.address != 0) {
				undoPatch(m_patch);
			}
		}
	}

	//-----------------------------------------------------------------------------
	// UI Functions

	void DisableFlashy::onPatchUI() {
		if (!address) {
			return;
		}

		if (ImGui::Checkbox("Disable Flashy", &m_choice)) {
			apply();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Disables flashy effect for items in inventory and fixes AstralWorld's varient if enabled");
		}
	}

	void DisableFlashy::onConfigLoad(const Config& cfg) {
		m_choice = cfg.get<bool>("DisableFlashy.Choice").value_or(false);

		if (m_choice != 0) {
			apply();
		}
	}

	void DisableFlashy::onConfigSave(Config& cfg) {
		cfg.set<bool>("DisableFlashy.Choice", m_choice);
	}
}