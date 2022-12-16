#include <Scan.hpp>

#include "Log.hpp"
#include <imgui.h>
#include "DisplayNamesFar.hpp"

//-----------------------------------------------------------------------------
// Static variable initialization
const static float VISIONRANGE = 30000;
//-----------------------------------------------------------------------------
// Constructor
namespace kanan{
	DisplayNamesFar::DisplayNamesFar(void)
	{
		
		//	Vision range 1
		address1 = scan("Pleione.dll", "D9 05 64 1F DD 63 D8 5B");

		//	Vision range 2
		address2 = scan("Pleione.dll", "FB 00 00 00 D9 05 64 1F DD 63 D8");

		//	Name Transparency 1 (Props)
		address3 = scan("Pleione.dll", "7A 2C D9 46 40 DC 2D 28 AF DB 63 DC 35 B0 6D DB 63 D9 5D FC"); // 0xEB

		//	Name Transparency 2 (I forgot)
		address4 = scan("Pleione.dll", "7A 4D D9 86"); // EB

		//	Name Transparency 3 (Player Name)
		address5 = scan("Pleione.dll", "7A 73 D9 86"); // EB

		//	Name Transparency 4 (Guild Name)
		address6 = scan("Pleione.dll", "7A 12 D9 45 14 DC 2D 28 AF DB 63 DC 35 B0 6D DB 63 D9 5D 18 8B"); // EB

		// Name Transparency 5 (Item Name)
		address7 = scan("Pleione.dll", "7A 2C D9 46 40 DC 2D 28 AF DB 63 DC 35 B0 6D DB 63 D9 5D F8 D9"); // EB

		// Name Transparency 6 (Guild Name 2)
		address8 = scan("Pleione.dll", "7A 12 D9 45 14 DC 2D 28 AF DB 63 DC 35 B0 6D DB 63 D9 5D 18 57"); // EB

		// Vision Range (Item)
		address9 = scan("Pleione.dll", "74 15 A1 60 DF FB 63 8B 48"); // EB

		//	Vision range 3
		address10 = scan("Pleione.dll", "E0 D9 05 64 1F DD 63 D8");

		if (address1 && address2 && address3 && address4 && address5 && address6 && address7 && address8 && address9 && address10) {
			log("Got DisplayNamesFar");
			int16_t floatToArray[4];
			memcpy(floatToArray, &VISIONRANGE, sizeof(void*));
			m_patch[0].address = *address1;
			m_patch[0].bytes = { 0xD9, 0x05, floatToArray[0], floatToArray[1], floatToArray[2], floatToArray[3] };
			m_patch[1].address = *address2;
			m_patch[1].bytes = { 0xFB, 0x00, 0x00, 0x00, 0xD9, 0x05, floatToArray[0], floatToArray[1], floatToArray[2], floatToArray[3] };
			m_patch[2].address = *address3;
			m_patch[2].bytes = { 0xEB };
			m_patch[3].address = *address4;
			m_patch[3].bytes = { 0xEB };
			m_patch[4].address = *address5;
			m_patch[4].bytes = { 0xEB };
			m_patch[5].address = *address6;
			m_patch[5].bytes = { 0xEB };
			m_patch[6].address = *address7;
			m_patch[6].bytes = { 0xEB };
			m_patch[7].address = *address8;
			m_patch[7].bytes = { 0xEB };
			m_patch[8].address = *address9;
			m_patch[8].bytes = { 0xEB };
			m_patch[9].address = *address10;
			m_patch[9].bytes = { 0xE0, 0xD9, 0x05, floatToArray[0], floatToArray[1], floatToArray[2], floatToArray[3] };
		}
		else {
			log("Failed to find DisplayNamesFar address.");
		}
	}

	void DisplayNamesFar::apply() {
		if (m_choice) {
			for (int i = 0; i < 10; i++) {
				patch(m_patch[i]);
			}
		}
		else {
			for (int i = 0; i < 10; i++) {
				undoPatch(m_patch[i]);
			}
		}
	}

	//-----------------------------------------------------------------------------
	// UI Functions

	void DisplayNamesFar::onPatchUI() {
		if (!(address1 && address2 && address3 && address4 && address5 && address6 && address7 && address8 && address9 && address10)) {
			return;
		}

		if (ImGui::Checkbox("Display Names Far", &m_choice)) {
			apply();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Displays names from far away");
		}
	}

	void DisplayNamesFar::onConfigLoad(const Config& cfg) {
		m_choice = cfg.get<bool>("DisplayNamesFar.Enabled").value_or(false);

		if (m_choice != 0) {
			apply();
		}
	}

	void DisplayNamesFar::onConfigSave(Config& cfg) {
		cfg.set<bool>("DisplayNamesFar.Enabled", m_choice);
	}
}