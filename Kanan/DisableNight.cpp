#include <Scan.hpp>

#include "Log.hpp"
#include <imgui.h>
#include "DisableNight.h"

namespace kanan {

	DisableNight::DisableNight()
	{
		// Disable Flashy for items in inventory and on ground
		address = scan("Renderer2.dll", "8B 01 D9 44 24 04 D9 58 0C");

		if (address) {
			log("Got Disable Night %p", *address);
		}
		else {
			log("Failed to find Disable Flashy address.");
		}
	}

	//-----------------------------------------------------------------------------
	// Hook functions

	__declspec(naked) void patchNighttime(void)
	{
		__asm {
			MOV		EAX, DWORD PTR DS : [ECX]
			MOV		ECX, 3E2AAAADh				// 0x3E2AAAAD = 1/6 = 4:00
			CMP		ECX, DWORD PTR SS : [ESP + 4h]
			JA		jmp_exit
			MOV		ECX, 3F400000h				// 0x3F400000 = 3/4 = 18:00
			CMP		ECX, DWORD PTR SS : [ESP + 4h]
			JB		jmp_exit
			MOV		ECX, DWORD PTR SS : [ESP + 4h]
			jmp_exit :								// ECX = time to set
			MOV		DWORD PTR DS : [EAX + 0Ch] , ECX
			RETN	4h
		}
	}

	void DisableNight::apply() {
		if (m_choice) {
			Hookjmp((void*)(*address), patchNighttime, 8);
		}
		else {
			Patchmem((BYTE*)(*address), (BYTE*)"\x8B\x01\xD9\x44\x24\x04\xD9\x58\x0C", 8);
		}
	}

	//-----------------------------------------------------------------------------
	// UI Functions

	void DisableNight::onPatchUI() {
		if (!address) {
			return;
		}

		if (ImGui::Checkbox("Disable Night", &m_choice)) {
			apply();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Disables night; Constant Daytime");
		}
	}

	void DisableNight::onConfigLoad(const Config& cfg) {
		m_choice = cfg.get<bool>("DisableNight.Enabled").value_or(false);

		if (m_choice != 0) {
			apply();
		}
	}

	void DisableNight::onConfigSave(Config& cfg) {
		cfg.set<bool>("DisableNight.Enabled", m_choice);
	}
}