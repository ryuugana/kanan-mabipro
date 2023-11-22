#include "Kanan.hpp"
#include "ScrollingMessageToChat.hpp"
#include "MabiPacket.h"
#include "imgui.h"
#include "Log.hpp"

namespace kanan {
	ScrollingMessageToChat::ScrollingMessageToChat()
	{
		m_isEnabled = false;
		m_isAuctionEnabled = false;
		m_isFieldBossEnabled = false;
		m_isFieldBNotifyEnabled = false;
		m_op.push_back(21101);
	}

	void ScrollingMessageToChat::onUI() {
		if (ImGui::CollapsingHeader("Scrolling Messages To Chat")) {
			ImGui::TextWrapped("This mod moves scrolling messages from the top of the screen to the middle of the screen and chat as <System> messages.");
			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
			if (ImGui::Checkbox("Enable Auction Messages To Chat", &m_isAuctionEnabled))
				m_isEnabled = m_isAuctionEnabled || m_isFieldBossEnabled;
			if (ImGui::Checkbox("Enable Field Boss Messages To Chat", &m_isFieldBossEnabled))
				m_isEnabled = m_isAuctionEnabled || m_isFieldBossEnabled;
			if (m_isFieldBossEnabled) {
				if (ImGui::Checkbox("Enable Field Boss Notification", &m_isFieldBNotifyEnabled))
					m_isEnabled = m_isAuctionEnabled || m_isFieldBossEnabled || m_isFieldBNotifyEnabled;
			}
		}
	}

	void ScrollingMessageToChat::onConfigLoad(const Config& cfg) {
		m_isAuctionEnabled = cfg.get<bool>("AuctionMessageToChat.Enabled").value_or(false);
		m_isFieldBossEnabled = cfg.get<bool>("FieldBossMessageToChat.Enabled").value_or(false);
		m_isFieldBNotifyEnabled = cfg.get<bool>("FieldBossNotify.Enabled").value_or(false);
		m_isEnabled = m_isAuctionEnabled || m_isFieldBossEnabled;
	}

	void ScrollingMessageToChat::onConfigSave(Config& cfg) {
		cfg.set<bool>("AuctionMessageToChat.Enabled", m_isAuctionEnabled);
		cfg.set<bool>("FieldBossMessageToChat.Enabled", m_isFieldBossEnabled);
		cfg.set<bool>("FieldBossNotify.Enabled", m_isFieldBNotifyEnabled);
	}

	void notify() {
		if (!FlashWindowEx) {
			HINSTANCE hLib = GetModuleHandleA("user32");
			if (hLib != NULL)
				(DWORD&)FlashWindowEx = (DWORD)GetProcAddress(hLib, "FlashWindowEx");
		}
		if (FlashWindowEx) {
			FLASHWINFO fInfo;
			fInfo.cbSize = sizeof(fInfo);
			fInfo.dwFlags = FLASHW_TRAY | FLASHW_TIMERNOFG;
			fInfo.hwnd = g_kanan->getWindow();
			fInfo.uCount = 0;
			fInfo.dwTimeout = 2500;
			FlashWindowEx(&fInfo);
		}
	}

	void ScrollingMessageToChat::onRecv(MabiMessage mabiMessage) {
		CMabiPacket recvPacket;
		recvPacket.SetSource(mabiMessage.buffer, mabiMessage.size);

		if (recvPacket.GetElement(0)->byte8 != 1 && recvPacket.GetElement(0)->byte8 != 8)
			return;

		if (m_isAuctionEnabled && strstr(recvPacket.GetElement(1)->str, "Channel 1")) {
			PacketData data;
			data.type = 1;
			data.byte8 = 7;
			recvPacket.SetElement(&data, 0);
			data.type = T_INT;
			data.int32 = 0;
			recvPacket.SetElement(&data, 2);

			BYTE* p;
			int tmpSizw = recvPacket.BuildPacket(&p);

			memcpy(mabiMessage.buffer, p, tmpSizw);
		}
		else if((m_isFieldBossEnabled && strstr(recvPacket.GetElement(1)->str, "has appeared")) ||
			(m_isFieldBossEnabled && strstr(recvPacket.GetElement(1)->str, "has defeated"))) {
			PacketData data;
			data.type = T_BYTE;
			data.byte8 = 7;
			recvPacket.SetElement(&data, 0);

			BYTE* p;
			int tmpSizw = recvPacket.BuildPacket(&p);

			memcpy(mabiMessage.buffer, p, tmpSizw);

			if (strstr(recvPacket.GetElement(1)->str, "has appeared") && m_isFieldBNotifyEnabled)
				notify();
		}
	}
}