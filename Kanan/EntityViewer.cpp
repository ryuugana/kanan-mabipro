#include "EntityViewer.hpp"

#include "imgui.h"

#include "Kanan.hpp"
#include "Creature.hpp"
#include "Prop.hpp"

#include <vector>
#include <mutex>
#include <memory>
#include <algorithm>

namespace kanan
{
	EntityViewer::EntityViewer()
	{
		m_hasSend = false;
		m_hasRecv = true;
		m_isEnabled = false;
		m_op.push_back(0x520C);
        m_op.push_back(0x52D0);
        m_op.push_back(0x5334);
	}

	void EntityViewer::onUI() {
		if (ImGui::TreeNode("Entity Viewer")) {
			ImGui::TextWrapped("This mod keeps track of entity information while enabled.");
			ImGui::TextWrapped("This can be useful for seeing what a character is wearing or hidden values like conditions.");
			ImGui::TextWrapped("The window can be moved and resized.");
			ImGui::Dummy(ImVec2{ 10.0f, 10.0f });

			ImGui::Checkbox("Enable Entity Viewer", &m_isEnabled);
			ImGui::BeginDisabled(!m_isEnabled);
			ImGui::Checkbox("Show Window", &m_window);
			ImGui::BeginDisabled(!m_window);
			ImGui::Checkbox("Show when Kanan is closed", &m_kananClosed);
			ImGui::EndDisabled();
			ImGui::EndDisabled();
			ImGui::Dummy(ImVec2{ 5.0f, 5.0f });
			ImGui::TreePop();
		}
	}

	bool EntityViewer::onWindow() {
		if (m_isEnabled && m_window) {
			if (m_kananClosed || g_kanan->isUIOpen())
			{
				eWindow.Draw(&m_window, entities, entitiesMutex);
				return true;
			}
		}

		return false;
	}

	void EntityViewer::onConfigLoad(const Config& cfg) {
		m_isEnabled = cfg.get<bool>("EntityViewer.Enabled").value_or(false);
		m_window = cfg.get<bool>("EntityViewer.Window").value_or(false);
		m_kananClosed = cfg.get<bool>("EntityViewer.KClosed").value_or(false);
	}

	void EntityViewer::onConfigSave(Config& cfg) {
		cfg.set<bool>("EntityViewer.Enabled", m_isEnabled);
		cfg.set<bool>("EntityViewer.Window", m_window);
		cfg.set<bool>("EntityViewer.KClosed", m_kananClosed);
	}

    void EntityViewer::onRecv(MabiMessage msg) {
        unsigned int op = GetOP(msg.buffer);
        if (op == 0x520C) {
            AddCreatureInfo(CMabiPacket(msg.buffer, msg.size));
        }
        else if (op == 0x52D0) {
            AddProp(CMabiPacket(msg.buffer, msg.size));
        }
        else if (op == 0x5334) {
			CMabiPacket mPacket(msg.buffer, msg.size);
			
			int count = 1;
			for (int i = 0; i < mPacket.GetElement(0)->word16; ++i)
			{
				short type = mPacket.GetElement(count++)->word16;
				int len = mPacket.GetElement(count++)->int32;
				auto binary = mPacket.GetElement(count++);
				CMabiPacket entityData((unsigned char*)binary->str, binary->len);

				// Creature
				if (type == 16)
				{
					AddCreatureInfo(entityData);
				}
				// Prop
				else if (type == 160)
				{
					AddProp(entityData);
				}
			}
        }
    }

    void EntityViewer::AddCreatureInfo(CMabiPacket packet) {
        auto creature = std::make_shared<Creature>();
		int p = 0;
		int count = 0;

		creature->EntityId = packet.GetElement(p++)->ID;
		creature->Type = packet.GetElement(p++)->byte8;

		// Public
		if (creature->Type != 5)
			return;

		creature->Name = packet.GetElement(p++)->str;
		p += 2;

		creature->Race = packet.GetElement(p++)->int32;
		creature->SkinColor = packet.GetElement(p++)->byte8;

		// [180600, NA187 (25.06.2014)] Changed from byte to short
		if (packet.GetElement(p)->type == T_BYTE)
			creature->EyeType = packet.GetElement(p++)->byte8;
		else if (packet.GetElement(p)->type == T_SHORT)
			creature->EyeType = packet.GetElement(p++)->word16;

		creature->EyeColor = packet.GetElement(p++)->byte8;
		creature->MouthType = packet.GetElement(p++)->byte8;
		creature->State = packet.GetElement(p++)->int32;
		// Public only
		if (creature->Type == 5)
		{
			creature->StateEx = packet.GetElement(p++)->int32;

			// [180300, NA166 (18.09.2013)]
			if (packet.GetElement(p)->type == T_INT)
				packet.GetElement(p++)->int32;
		}
		creature->Height = packet.GetElement(p++)->float32;
		creature->Weight = packet.GetElement(p++)->float32;
		creature->Upper = packet.GetElement(p++)->float32;
		creature->Lower = packet.GetElement(p++)->float32;
		creature->Region = packet.GetElement(p++)->int32;
		creature->X = packet.GetElement(p++)->int32;
		creature->Y = packet.GetElement(p++)->int32;
		creature->Direction = packet.GetElement(p++)->byte8;
		creature->BattleState = packet.GetElement(p++)->int32;
		creature->WeaponSet = packet.GetElement(p++)->byte8;
		creature->Color1 = packet.GetElement(p++)->int32;
		creature->Color2 = packet.GetElement(p++)->int32;
		creature->Color3 = packet.GetElement(p++)->int32;
		creature->CombatPower = packet.GetElement(p++)->float32;
		creature->StandStyle = packet.GetElement(p++)->str;

		// [200400, NA267 (2018-01-11)] OddEye support
		if (packet.GetElement(p)->type == T_BYTE)
		{
			packet.GetElement(p++)->byte8;
			packet.GetElement(p++)->byte8;
		}

		// [210300, NA292 (2018-12-07)] ?
		if (packet.GetElement(p)->type == T_SHORT)
		{
			packet.GetElement(p++)->word16;
			packet.GetElement(p++)->int32;
		}

		// [250100, NA360 (2020-12-19)] ?
		if (packet.GetElement(p)->type == T_SHORT)
		{
			packet.GetElement(p++)->word16;
		}

		creature->LifeRaw = packet.GetElement(p++)->float32;
		creature->LifeMaxBase = packet.GetElement(p++)->float32;
		creature->LifeMaxMod = packet.GetElement(p++)->float32;
		creature->LifeInjured = packet.GetElement(p++)->float32;

		// [180800, NA196 (14.10.2014)] ?
		if (packet.GetElement(p)->type == T_SHORT)
			packet.GetElement(p++)->word16; // ?

		// [220100, NA293 (2019-01-12)] ? (same as in private?)
		if (packet.GetElement(p)->type == T_FLOAT)
		{
			packet.GetElement(p++)->float32;
			packet.GetElement(p++)->float32;
		}

		int regenCount = packet.GetElement(p++)->int32;
		for (int i = 0; i < regenCount; ++i)
		{
			packet.GetElement(p++)->int32;
			packet.GetElement(p++)->float32;
			packet.GetElement(p++)->int32;
			packet.GetElement(p++)->int32;
			packet.GetElement(p++)->byte8;
			packet.GetElement(p++)->float32;
			if (packet.GetElement(p)->type == T_BYTE)
				packet.GetElement(p++)->byte8; // [200300, NA262 (2017-10-20)] ?
		}

		int unkCount = packet.GetElement(p++)->int32;
		for (int i = 0; i < unkCount; ++i)
		{
			p+=6;
		}

		if (packet.GetElement(p)->type == T_SHORT)
			creature->Title = packet.GetElement(p++)->word16;
		else
			creature->Title = packet.GetElement(p++)->int32;

		creature->TitleApplied = packet.GetElement(p++)->ID;

		if (packet.GetElement(p)->type == T_SHORT)
			creature->OptionTitle = packet.GetElement(p++)->word16;
		else
			creature->OptionTitle = packet.GetElement(p++)->int32;

		creature->MateName = packet.GetElement(p++)->str;
		creature->Destiny = packet.GetElement(p++)->byte8;

		// [250200, NA371 (2021-07-16)] ?
		if (packet.GetElement(p)->type == T_SHORT && packet.GetElement(p+1)->type == T_INT)
		{
			packet.GetElement(p++)->word16;
			packet.GetElement(p++)->int32;
		}

		unsigned int itemCount = packet.GetElement(p++)->int32;
		for (int i = 0; i < itemCount; ++i)
		{
			auto itemOId = packet.GetElement(p++)->ID;

			ItemInfo itemInfo = *(ItemInfo*)packet.GetElement(p++)->str;

			if (packet.GetElement(p)->type == T_STRING)
				packet.GetElement(p++)->str; // Extra Item Info
			creature->Items.try_emplace(itemOId, itemInfo);
		}

		AddEntity(creature);
    }

    void EntityViewer::AddProp(CMabiPacket packet) {
        auto prop = std::make_shared<Prop>();
		int p = 0;

		prop->EntityId = packet.GetElement(p++)->ID;
		prop->Id = packet.GetElement(p++)->int32;

		if (prop->IsServerProp())
		{
			prop->Name = packet.GetElement(p++)->str;
			prop->Title = packet.GetElement(p++)->str;
			prop->Info = *(PropInfo*)packet.GetElement(p++)->str;
		}

		prop->State = packet.GetElement(p++)->str;
		packet.GetElement(p++)->ID;

		if (packet.GetElement(p++)->byte8)
			prop->Xml = packet.GetElement(p++)->str;

		if (!prop->IsServerProp())
			prop->Direction = packet.GetElement(p++)->float32;

        AddEntity(prop);
    }

    void EntityViewer::AddEntity(std::shared_ptr<IEntity> entity) {
        if (CheckDuplicate(entity))
            return;

        {
            std::lock_guard<std::mutex> lock(entitiesMutex);
            entities.push_back(entity);
        }
    }

    bool EntityViewer::CheckDuplicate(const std::shared_ptr<IEntity>& newEntity) {
		std::lock_guard<std::mutex> lock(entitiesMutex);

		auto it = std::find_if(entities.begin(), entities.end(), [&newEntity](const std::shared_ptr<IEntity>& e) {
			return e->Equals(newEntity.get());
			});

		if (it != entities.end()) {
			*it = newEntity;
			return true;
		}

		return false;
    }
}