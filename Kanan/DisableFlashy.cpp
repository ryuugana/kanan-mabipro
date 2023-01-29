#include <Scan.hpp>

#include "Log.hpp"
#include <imgui.h>
#include "DisableFlashy.h"

namespace kanan {
	LPBYTE addrTargetReturn = NULL;
	unsigned int tempAddr = 0;
	unsigned int hexColorCode = 0;
	unsigned int hexColorCode1 = 0;
	unsigned int hexColorCode2 = 0;
	unsigned int hexColorCode3 = 0;
	unsigned int tempDiff = 0;


	//-----------------------------------------------------------------------------
	// Hook functions

	__declspec(naked) void patchFlashyValue(void)
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


	__declspec(naked) void patchFlashyValue2(void)
	{
		__asm {
			mov     eax, [ebp + 0x18]
			mov     ecx, [eax]
			mov		tempAddr, eax
			mov		hexColorCode1, ecx
			mov		ecx, [eax + 0x04]
			mov		hexColorCode2, ecx
			mov		ecx, [eax + 0x08]
			mov		hexColorCode3, ecx
		}

		if (hexColorCode1 >= 0x40000000 && hexColorCode1 <= 0x7F000000) {
			tempDiff = hexColorCode1 >> 24;
			tempDiff = tempDiff << 24;
			hexColorCode1 -= tempDiff;
			hexColorCode1 += 0x10000000;
		}

		if (hexColorCode2 >= 0x40000000 && hexColorCode2 <= 0x7F000000) {
			tempDiff = hexColorCode2 >> 24;
			tempDiff = tempDiff << 24;
			hexColorCode2 -= tempDiff;
			hexColorCode2 += 0x10000000;
		}

		if (hexColorCode3 >= 0x40000000 && hexColorCode3 <= 0x7F000000) {
			tempDiff = hexColorCode3 >> 24;
			tempDiff = tempDiff << 24;
			hexColorCode3 -= tempDiff;
			hexColorCode3 += 0x10000000;
		}

		__asm {
			mov		ecx, hexColorCode1
			mov		eax, tempAddr
			mov[eax], ecx
			mov		ecx, hexColorCode2
			mov[eax + 0x04], ecx
			mov		ecx, hexColorCode3
			mov[eax + 0x08], ecx
			mov     ecx, [eax]
			jmp		addrTargetReturn
		}
	}

	//-----------------------------------------------------------------------------
	// Initialize

	DisableFlashy::DisableFlashy()
	{

		// Disable Flashy for items in inventory and on ground
		address = scan("Standard.dll", "8B 44 24 04 8B 44 81 10 C2 04");
		address2 = scan("Pleione.dll", "8B 45 18 8B 08 89 4E 1C");

		if (address && address2) {
			log("Got Disable Flashy %p %p", *address, *address2);

			addrTargetReturn = (LPBYTE)*address2 + 5;

			m_patch.address = *address;
			m_patch.funcAddress = patchFlashyValue;
			m_patch.size = 8;
			m_patch.type = P_JMP;

			m_patch2.address = *address2;
			m_patch2.funcAddress = patchFlashyValue2;
			m_patch2.size = 5;
			m_patch2.type = P_JMP;
		}
		else {
			log("Failed to find Disable Flashy address.");
		}

		// AstralWorld AntiFlashy Fix
		/*auto fix = scan("mss32.dll", "81 E9 00 00 00 01");

		if (fix) {
			log("Got Disable Flashy Fix %p", *fix);
			m_patch.address = *fix;
			m_patch.bytes = { 0x81, 0xC1, 0x00, 0x00, 0x00, 0x10 };
		}
		else {
			log("Failed to find Disable Flashy Fix address.");
		}*/
	}

	//-----------------------------------------------------------------------------
	// UI Functions

	void DisableFlashy::apply() {
		if (m_choice) {
			hookPatch(m_patch);
			hookPatch(m_patch2);
			//Hookjmp((void*)(*address2), patchFlashyValue2, 5);
		}
		else {
			undoPatch(m_patch);
			undoPatch(m_patch2);
			//Patchmem((BYTE*)(*address), (BYTE*)"\x8B\x44\x24\x04\x8B\x44\x81\x10", 8);
			//Patchmem((BYTE*)(*address2), (BYTE*)"\x8B\x45\x18\x8B\x08\x89\x4E\x1C", 8);
		}
	}

	void DisableFlashy::onPatchUI() {
		if (!address) {
			return;
		}

		if (ImGui::Checkbox("Disable Flashy", &m_choice)) {
			apply();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Disables flashy effects");
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