#include "EntityWindow.hpp"
#include "imgui.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace kanan
{
EntityWindow::EntityWindow() {}

void EntityWindow::Clear() {
    m_selectedIdx = -1;
    m_infoText.clear();
}

void EntityWindow::UpdateSelection(const std::shared_ptr<IEntity>& entity) {
    if (entity) {
        m_infoText = entity->GetInfo();
    }
    else {
        m_infoText.clear();
    }
}

void EntityWindow::Draw(bool* p_open, std::vector<std::shared_ptr<IEntity>>& entities, std::mutex& entitiesMutex) {
    if (!p_open || !*p_open) return;

    ImGui::SetNextWindowSize(ImVec2(850, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Entity Logger", p_open)) {
        ImGui::End();
        return;
    }

    // --- Top Action Bar ---
    if (ImGui::Button("Clear")) {
        std::lock_guard<std::mutex> lock(entitiesMutex);
        entities.clear();
        Clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Info")) {
        m_showAboutModal = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        *p_open = false;
    }

    ImGui::Separator();

    // --- Main Layout Split: Left (List) / Right (Details) ---
    float leftWidth = ImGui::GetContentRegionAvail().x * 0.45f;

    // Lock the entity vector while building the view
    std::lock_guard<std::mutex> lock(entitiesMutex);

    // Left Panel: Sorted Entity Table
    ImGui::BeginChild("EntityListRegion", ImVec2(leftWidth, 0), true);
    RenderTable(entities);
    ImGui::EndChild();

    ImGui::SameLine();

    // Right Panel: Info & Script Display
    ImGui::BeginChild("EntityDetailsRegion", ImVec2(0, 0), true);

    ImGui::TextUnformatted("Entity Information");
    ImVec2 boxSize(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
    ImGui::InputTextMultiline("##EntityInfo", m_infoText.data(), m_infoText.length(), boxSize, ImGuiInputTextFlags_ReadOnly);

    ImGui::EndChild();

    // --- Info Modal Window ---
    if (m_showAboutModal) {
        ImGui::OpenPopup("About Entity Logger");
        m_showAboutModal = false;
    }

    if (ImGui::BeginPopupModal("About Entity Logger", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Entity Logger reads all logged packets and displays\ninformation about the creatures and props found.");
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

void EntityWindow::RenderTable(const std::vector<std::shared_ptr<IEntity>>& entities) {
    static ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
        ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
        ImGuiTableFlags_ScrollY; // | ImGuiTableFlags_Selectable;

    if (ImGui::BeginTable("EntitiesTable", 3, flags)) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 130.0f, 0);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70.0f, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.0f, 2);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        // Handle Table Sorting
        if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
            if (sortSpecs->SpecsDirty && !entities.empty()) {
                m_sortColumn = sortSpecs->Specs[0].ColumnIndex;
                m_sortAscending = (sortSpecs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
                sortSpecs->SpecsDirty = false;
            }
        }

        // Build a sorted list of indices without mutating the original vector
        std::vector<size_t> indices(entities.size());
        for (size_t i = 0; i < indices.size(); ++i) indices[i] = i;

        std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
            const auto& itemA = entities[a];
            const auto& itemB = entities[b];

            int comp = 0;
            if (m_sortColumn == 0) comp = itemA->GetEntityType().compare(itemB->GetEntityType());
            else if (m_sortColumn == 1) comp = (itemA->GetEntityId() < itemB->GetEntityId()) ? -1 : ((itemA->GetEntityId() > itemB->GetEntityId()) ? 1 : 0);
            else if (m_sortColumn == 2) comp = itemA->GetName().compare(itemB->GetName());

            return m_sortAscending ? (comp < 0) : (comp > 0);
            });

        // Render Table Rows
        for (size_t i = 0; i < indices.size(); ++i) {
            size_t realIdx = indices[i];
            const auto& entity = entities[realIdx];

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            // Type
            bool isSelected = (m_selectedIdx == static_cast<int>(realIdx));

            // Need a unique ID for ImGui label
            std::ostringstream ss;
            ss << std::hex << std::uppercase << std::setfill('0') << std::setw(16) << entity->GetEntityId();
            if (ImGui::Selectable(ss.str().c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                m_selectedIdx = static_cast<int>(realIdx);
                UpdateSelection(entity);
            }

            // ID (Hex formatted)
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entity->GetEntityType().c_str());

            // Name
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entity->GetName().c_str());
        }

        ImGui::EndTable();
    }
}

}