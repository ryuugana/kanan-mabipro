#include "Hotkey.hpp"

#include <Scan.hpp>
#include <Utility.hpp>
#include <imgui_internal.h>

namespace kanan {
	int Hotkey::Display(const char* label, const ImVec2 size_arg)
	{
		ImGuiContext& g = *GImGui;
		ImGuiIO& io = g.IO;
		bool value_changed = false;
		int key = hotkey;
		if (m_isKeyBindOpen) {
			if (!value_changed) {
				for (auto i = VK_BACK; i <= VK_RMENU; i++) {
					if (wasKeyPressed(i)) {
						key = i;
						value_changed = true;
					}
				}
			}

			if (ImGui::IsKeyPressed(ImGuiKey_Escape) || key == VK_ESCAPE) {
				hotkey = 0;
				m_isKeyBindOpen = false;
			}
			else {
				hotkey = key;
			}
		}

		// Render
		// Select which buffer we are going to display. When ImGuiInputTextFlags_NoLiveEdit is Set 'buf' might still be the old value. We Set buf to NULL to prevent accidental usage from now on.

		char buf_display[64] = "None";

		if (hotkey != 0 && !m_isKeyBindOpen) {
			strcpy_s(buf_display, KeyNames[hotkey]);
		}
		else if (m_isKeyBindOpen) {
			strcpy_s(buf_display, "<Press a key>");
		}

		ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
		ImGui::Button(label, ImVec2(size_arg.x * 0.50f, size_arg.y));
		ImGui::PushItemFlag(ImGuiItemFlags_Disabled, false);
		ImGui::SameLine();
		if (m_isKeyBindOpen) {
			m_isKeyBindOpen = !value_changed;
		}

		if (ImGui::Button(buf_display, ImVec2(size_arg.x * 0.50f, size_arg.y))) {
			if (m_isKeyBindOpen == false) {
				m_isKeyBindOpen = true;
			}
		}

		return value_changed;
	}
}