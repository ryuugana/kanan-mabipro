#pragma once
#include "IEntity.hpp"
#include <vector>
#include <memory>
#include <mutex>
#include <string>

namespace kanan
{
class EntityWindow {
public:
    EntityWindow();

    // Renders the ImGui window (call this inside your main rendering/ImGui loop)
    void Draw(bool* p_open, std::vector<std::shared_ptr<IEntity>>& entities, std::mutex& entitiesMutex);

    void Clear();

private:
    int m_selectedIdx = -1;
    int m_sortColumn = 1;      // 0: Type, 1: ID, 2: Name
    bool m_sortAscending = true;

    std::string m_infoText;
    bool m_showAboutModal = false;

    void RenderTable(const std::vector<std::shared_ptr<IEntity>>& entities);
    void UpdateSelection(const std::shared_ptr<IEntity>& entity);
};
}