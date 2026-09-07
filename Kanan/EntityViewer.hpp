#pragma once

#include <chrono>

#include "IEntity.hpp"
#include "EntityWindow.hpp"
#include "MabiPacket.h"
#include "MessageMod.hpp"


namespace kanan {
	class EntityViewer : public MessageMod {
	public:
		EntityViewer();

		void onUI() override;

		bool onWindow() override;

		void onConfigLoad(const Config& cfg) override;
		void onConfigSave(Config& cfg) override;

		void onRecv(MabiMessage mabiMessage) override;

	private:
		void drawWindow();

        void AddCreatureInfo(CMabiPacket packet);
		void AddProp(CMabiPacket packet);
		void AddEntity(std::shared_ptr<IEntity> entity);

		bool CheckDuplicate(const std::shared_ptr<IEntity>& newEntity);

		EntityWindow eWindow;
		std::vector<std::shared_ptr<IEntity>> entities;
		std::mutex entitiesMutex;
	};
}