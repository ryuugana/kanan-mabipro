#include "GetInfo.hpp"
#include "MabiPacket.h"
#include "imgui.h"
#include "Log.hpp"
#include "Kanan.hpp"

namespace kanan {
	GetInfo::GetInfo()
	{
		m_hasSend = false;
		m_hasRecv = true;
		m_isEnabled = true;
		m_op.push_back(0x909A); // Nao count login packet
	}

	void GetInfo::onRecv(MabiMessage mabiMessage) {
		CMabiPacket recvPacket;
		recvPacket.SetSource(mabiMessage.buffer, mabiMessage.size);
		if (recvPacket.GetReciverId() < 0x10010000000000)
		{
			// Find out who we are on login
			if (recvPacket.GetOP() == 0x909A)
			{
				g_kanan->characterId = recvPacket.GetReciverId();
			}
		}
	}
}