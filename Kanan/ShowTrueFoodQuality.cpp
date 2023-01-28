#include <Scan.hpp>

#include "Log.hpp"
#include <imgui.h>
#include "ShowTrueFoodQuality.hpp"


namespace kanan{
	//-----------------------------------------------------------------------------
	// Static variable initialization

	wchar_t dataBuf[256];
	LPBYTE addrCStringTEquals = NULL;
	LPBYTE aaddrTargetReturn = NULL;
	BYTE hexstr[32];

	//-----------------------------------------------------------------------------
	// Constructor

	ShowTrueFoodQuality::ShowTrueFoodQuality(void)
	{
		address = scan("Pleione.dll", "? ? ? ? ? ? ? ? 80 7d 18 00 0f 84 ? ? ? ? 83 7D 0C 50");

		if (address) {

			for (int i = 0; i < 8; i++) {
				hexstr[i] = ((LPBYTE)*address)[i];
				log("%02X", hexstr[i]);
			}

			int offset = (((LPBYTE)*address)[7] << 24) | (((LPBYTE)*address)[6] << 16) | (((LPBYTE)*address)[5] << 8) | (((LPBYTE)*address)[4]);

			addrCStringTEquals = (LPBYTE)*address+offset+8;
			aaddrTargetReturn = (LPBYTE)*address + 8;
			log("Found Show True Food Quality %p %p", *address, addrCStringTEquals);
		}
		else {
			log("Failed to find Show True Food Quality address.");
		}
	}

	//-----------------------------------------------------------------------------
	// Hook functions

	void patchFoodQualityFormat(wchar_t * buff, wchar_t* str, int d)
	{
		swprintf_s(buff, 255, L"%s (%d)", str, d);
	}

	__declspec(naked) void patchFoodQuality(void)
	{
		//EAX: Numeric food quality
		//EBX: String pointer
		//ECX: none
		__asm {
			pop		ecx
			push	eax
			push	ecx
			push	offset dataBuf
			call	patchFoodQualityFormat
			add		esp, 0Ch

			push	offset dataBuf
			mov		ecx, ebx;
			call	addrCStringTEquals

			jmp		aaddrTargetReturn
		}
	}


	//-----------------------------------------------------------------------------
	// UI Functions

	void ShowTrueFoodQuality::apply() {
		if (m_choice) {
			Hookjmp((void*)(*address), patchFoodQuality, 8);
		}
		else {
			Patchmem((BYTE*)(*address), hexstr, 8);
		}
	}

	void ShowTrueFoodQuality::onPatchUI() {
		if (!address) {
			return;
		}

		if (ImGui::Checkbox("Show True Food Quality", &m_choice)) {
			apply();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Shows food quality in numerical format");
		}
	}

	void ShowTrueFoodQuality::onConfigLoad(const Config& cfg) {
		m_choice = cfg.get<bool>("ShowTrueFoodQuality.Enabled").value_or(false);

		if (m_choice != 0) {
			apply();
		}
	}

	void ShowTrueFoodQuality::onConfigSave(Config& cfg) {
		cfg.set<bool>("ShowTrueFoodQuality.Enabled", m_choice);
	}
}