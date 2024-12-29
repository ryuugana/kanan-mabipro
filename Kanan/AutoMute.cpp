#include <Scan.hpp>

#include "Log.hpp"
#include <imgui.h>
#include "AutoMute.hpp"

namespace kanan {
	LPVOID CMSS;
	LONG cmssProcessJmpTarget = NULL;

	typedef bool(__thiscall* MuteSignature)(LPVOID CMSS, bool mute);

	MuteSignature Mute = NULL;

	AutoMute::AutoMute()
	{
		// esl::CMSS::Process(CMSS *this)
		address = scan("EXL.dll", "56 8b f1 80 7e 04 00 74 24 8b");

		if (address) {
			log("Got CMSS::Process %p", *address);

			DWORD processFunctionAddress = *address;
			LONG processFunctionAddressLong = *(LONG*)(void*)(&processFunctionAddress);
			cmssProcessJmpTarget = processFunctionAddressLong + 9;
		}
		else {
			log("Failed to find CMSS::Process address.");
		}


		// esl::CMSS::Mute(CMSS *this, bool mute)
		std::optional<uintptr_t> MuteFunctionAddressPtr = scan("EXL.dll", "56 8b f1 80 7e 04 00 75 04 32 c0 eb 2b");

		if (MuteFunctionAddressPtr) {
			log("Got CMSS::Mute %p", *MuteFunctionAddressPtr);

			DWORD MuteFunctionAddress = *MuteFunctionAddressPtr;
			LONG MuteFunctionAddressLong = *(LONG*)(void*)(&MuteFunctionAddress);

			Mute = (MuteSignature)MuteFunctionAddressLong;
		}
		else {
			log("Failed to find CMSS::Mute address.");
		}
	}

	VOID MuteWhenBackground(LPVOID cmss) {
		bool inBackground = GetActiveWindow() != GetForegroundWindow();
		Mute(cmss, inBackground);
	}

	//-----------------------------------------------------------------------------
	// Hook functions

	__declspec(naked) void hookCMSSProcess()
	{
		__asm {
			mov		CMSS, ecx
		}

		MuteWhenBackground(CMSS);

		__asm {
			push	esi
			mov		esi, CMSS
			cmp		byte ptr[esi + 4], 00
			je		cmssProcessRet
			jmp		cmssProcessJmpTarget
			cmssProcessRet:
			pop		esi
			ret
		}
	}
	/*
			*/
	void AutoMute::apply() {
		if (m_choice) {
			Hookjmp((void*)(*address), hookCMSSProcess, 9);
		}
		else {
			Patchmem((BYTE*)(*address), (BYTE*)"\x56\x8b\xf1\x80\x7e\x04\x00\x74\x24", 9);
		}
	}

	//-----------------------------------------------------------------------------
	// UI Functions

	void AutoMute::onPatchUI() {
		if (!address) {
			return;
		}

		if (ImGui::Checkbox("Mute Mabinogi in Background", &m_choice)) {
			apply();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Automatically mutes Mabinogi when tabbed out\nConvenient for multitasking");
		}
	}

	void AutoMute::onConfigLoad(const Config& cfg) {
		m_choice = cfg.get<bool>("AutoMute.Enabled").value_or(false);

		if (m_choice) {
			apply();
		}
	}

	void AutoMute::onConfigSave(Config& cfg) {
		cfg.set<bool>("AutoMute.Enabled", m_choice);
	}
}