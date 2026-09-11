#include "EntityWindow.hpp"
#include "imgui.h"
#include <algorithm>
#include <array>
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
        if (entity->GetEntityType() == "Prop")
        {
            if (auto prop = std::dynamic_pointer_cast<Prop>(entity)) {
                m_infoText = GetPropInfo(prop);
            }
        }
        else
        {
            if (auto creature = std::dynamic_pointer_cast<Creature>(entity)) {
                m_infoText = GetCreatureInfo(creature);
            }
        }
    }
    else {
        m_infoText.clear();
    }
}

void EntityWindow::Draw(bool* p_open, std::vector<std::shared_ptr<IEntity>>& entities, std::mutex& entitiesMutex) {
    if (!p_open || !*p_open) return;

    ImGui::SetNextWindowSize(ImVec2(920, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Entity Viewer", p_open)) {
        ImGui::End();
        return;
    }

    // --- Top Action Bar ---
    ImGui::Dummy(ImVec2{ 5.0f, 5.0f });
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        std::lock_guard<std::mutex> lock(entitiesMutex);
        entities.clear();
        Clear();
    }
    // Viewing options for prop / mob / npc / people / pet as checkboxes
    ImGui::SameLine();
    ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
    ImGui::SameLine();
    ImGui::Checkbox("Player", &m_player);
    ImGui::SameLine();
    ImGui::Checkbox("Pet", &m_pet);
    ImGui::SameLine();
    ImGui::Checkbox("NPC", &m_npc);
    ImGui::SameLine();
    ImGui::Checkbox("Monster", &m_mob);
    ImGui::SameLine();
    ImGui::Checkbox("Prop", &m_prop);

    ImGui::Separator();

    // --- Main Layout Split: Left (List) / Right (Details) ---
    float leftWidth = ImGui::GetContentRegionAvail().x * 0.39f;

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
    ImVec2 boxSize(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y * 0.46f);
    ImGui::InputTextMultiline("##EntityInfo", m_infoText.data(), m_infoText.length(), boxSize, ImGuiInputTextFlags_ReadOnly);

    ImGui::Spacing();

    ImGui::TextUnformatted("Equipped Items");

    static ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY;

    // Pass the remaining height to BeginTable so it fills the bottom section
    if (ImGui::BeginTable("EquippedItemsTable", 5, flags, ImVec2(0.0f, ImGui::GetContentRegionAvail().y))) {
        // Setup Header Columns
        ImGui::TableSetupColumn("Pocket", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Item ID", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Color 1", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Color 2", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Color 3", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        // Helper lambda to render a color swatch next to the hex text
        auto RenderColoredCell = [](const char* idStr, uint32_t argbColor) {
            uint8_t alphaFlag = (argbColor >> 24) & 0xFF;

            // Extract RGB components
            float r = ((argbColor >> 16) & 0xFF) / 255.0f;
            float g = ((argbColor >> 8) & 0xFF) / 255.0f;
            float b = (argbColor & 0xFF) / 255.0f;

            ImVec4 colorVec(r, g, b, 1.0f); // Always display base RGB at full opacity

            // Render the color swatch
            ImGui::ColorButton(idStr, colorVec, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(14, 14));
            ImGui::SameLine();

            ImGui::Text("0x%08X", argbColor);
            };

        // Safely cast the selected IEntity to a Creature pointer
        auto creature = (m_selectedIdx >= 0 && m_selectedIdx < static_cast<int>(entities.size()))
            ? std::dynamic_pointer_cast<Creature>(entities[m_selectedIdx])
            : nullptr;

        if (creature) {
            std::vector<ItemInfo> sortedItems;
            sortedItems.reserve(creature->Items.size());
            for (const auto& [id, item] : creature->Items) {
                sortedItems.push_back(item);
            }

            std::sort(sortedItems.begin(), sortedItems.end(), [this](const ItemInfo& a, const ItemInfo& b) {
                return GetPocketName(a.Pocket) < GetPocketName(b.Pocket);
                });


            int rowId = 0;
            for (const auto& item : sortedItems) {
                ImGui::TableNextRow();
                ImGui::PushID(rowId++);

                // Pocket Name
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(GetPocketName(item.Pocket).c_str());

                // Class ID
                ImGui::TableNextColumn();
                ImGui::Text("%d", item.Id);

                // Color 1
                ImGui::TableNextColumn();
                RenderColoredCell("##C1", item.Color1);

                // Color 2
                ImGui::TableNextColumn();
                RenderColoredCell("##C2", item.Color2);

                // Color 3
                ImGui::TableNextColumn();
                RenderColoredCell("##C3", item.Color3);

                ImGui::PopID();
            }
        }

        ImGui::EndTable();
    }

    ImGui::EndChild();

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
            else if (m_sortColumn == 2)
            {
                std::string charNameA = itemA->GetName();
                std::string charNameB = itemB->GetName();
                std::transform(charNameA.begin(), charNameA.end(), charNameA.begin(), ::tolower);
                std::transform(charNameB.begin(), charNameB.end(), charNameB.begin(), ::tolower);
                comp = charNameA.compare(charNameB);
            }

            return m_sortAscending ? (comp < 0) : (comp > 0);
            });

        // Render Table Rows
        for (size_t i = 0; i < indices.size(); ++i) {
            size_t realIdx = indices[i];
            const auto& entity = entities[realIdx];

            if (!m_player && entity.get()->GetEntityType() == "Player")
                continue;
            else if (!m_pet && entity.get()->GetEntityType() == "Pet")
                continue;
            else if (!m_npc && entity.get()->GetEntityType() == "NPC")
                continue;
            else if (!m_mob && entity.get()->GetEntityType() == "Monster")
                continue;
            else if (!m_prop && entity.get()->GetEntityType() == "Prop")
                continue;

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


std::string EntityWindow::GetCreatureInfo(const std::shared_ptr<Creature>& creature) {
    std::ostringstream sb;
    sb << std::fixed << std::setprecision(2);

    float h = (creature->Height < 1.0f && creature->Height > 0.999f) ? 1.0f : creature->Height;
    float w = (creature->Weight < 1.0f && creature->Weight > 0.999f) ? 1.0f : creature->Weight;
    float u = (creature->Upper < 1.0f && creature->Upper > 0.999f) ? 1.0f : creature->Upper;
    float l = (creature->Lower < 1.0f && creature->Lower > 0.999f) ? 1.0f : creature->Lower;

    sb << "Name: " << creature->Name << "\r\n";
    sb << "Race: " << std::dec << creature->Race << "\r\n\r\n";

    sb << "CP: " << creature->CombatPower << "\r\n";
    sb << "Life: " << creature->GetLife() << " (" << creature->LifeRaw << ") / " << creature->GetLifeMax() << " (" << creature->LifeMaxBase << ")\r\n\r\n";

    sb << "Conditions: ";
    for (int i = 0; i < creature->Conditions.size(); i++)
    {
        if (i > 0)
        {
            sb << ", ";
        }

        sb << GetConditionEngName(creature->Conditions[i]);
    }
    sb << "\r\n\r\n";

    sb << "Region: " << creature->Region << "\r\n";
    sb << "Position: " << creature->X << " / " << creature->Y << "\r\n";
    sb << "Direction: " << (int)creature->Direction << "\r\n\r\n";

    sb << "Title: " << creature->Title << "\r\n";
    sb << "Option title: " << creature->OptionTitle << "\r\n\r\n";

    sb << "Skin color: " << (int)creature->SkinColor << "\r\n";
    sb << "Eye type: " << creature->EyeType << "\r\n";
    sb << "Eye color: " << (int)creature->EyeColor << "\r\n";
    sb << "Mouth type: " << (int)creature->MouthType << "\r\n\r\n";

    sb << "Height: " << h << "\r\n";
    sb << "Weight: " << w << "\r\n";
    sb << "Upper:  " << u << "\r\n";
    sb << "Lower:  " << l << "\r\n\r\n";

    sb << "Color 1: 0x" << std::hex << std::setfill('0') << std::setw(8) << creature->Color1 << "\r\n";
    sb << "Color 2: 0x" << std::setfill('0') << std::setw(8) << creature->Color2 << "\r\n";
    sb << "Color 3: 0x" << std::setfill('0') << std::setw(8) << creature->Color3 << "\r\n\r\n";

    sb << "Stand style: " << creature->StandStyle << "\r\n\r\n";

    return sb.str();
}

std::string EntityWindow::GetPropInfo(const std::shared_ptr<Prop>& prop) {
    std::ostringstream sb;
    sb << (prop->IsServerProp() ? "Server" : "Client") << " sided prop\r\n\r\n";

    sb << "Entity id: " << std::hex << std::uppercase << std::setfill('0') << std::setw(16) << prop->EntityId << "\r\n";
    sb << "Prop id: " << std::dec << prop->Id << "\r\n";
    sb << "State: " << prop->State << "\r\n";
    sb << "XML: " << prop->Xml << "\r\n";

    if (prop->IsServerProp()) {
        sb << "Name: " << prop->Name << "\r\n";
        sb << "Title: " << prop->Title << "\r\n";
        sb << "Info: \r\n";
        sb << "   Altitude: " << prop->Info.Altitude << "\r\n";
        sb << "   Color1: 0x" << std::hex << std::setfill('0') << std::setw(8) << prop->Info.Color1 << "\r\n";
        sb << "   Color2: 0x" << std::setfill('0') << std::setw(8) << prop->Info.Color2 << "\r\n";
        sb << "   Color3: 0x" << std::setfill('0') << std::setw(8) << prop->Info.Color3 << "\r\n";
        sb << "   Color4: 0x" << std::setfill('0') << std::setw(8) << prop->Info.Color4 << "\r\n";
        sb << "   Color5: 0x" << std::setfill('0') << std::setw(8) << prop->Info.Color5 << "\r\n";
        sb << "   Color6: 0x" << std::setfill('0') << std::setw(8) << prop->Info.Color6 << "\r\n";
        sb << "   Color7: 0x" << std::setfill('0') << std::setw(8) << prop->Info.Color7 << "\r\n";
        sb << "   Color8: 0x" << std::setfill('0') << std::setw(8) << prop->Info.Color8 << "\r\n";
        sb << "   Color9: 0x" << std::setfill('0') << std::setw(8) << prop->Info.Color9 << "\r\n";
        sb << "   Direction: " << std::dec << prop->Info.Direction << "\r\n";
        sb << "   FixedAltitude: " << prop->Info.FixedAltitude << "\r\n";
        sb << "   Id: " << prop->Info.Id << "\r\n";
        sb << "   Region: " << prop->Info.Region << "\r\n";
        sb << "   Scale: " << prop->Info.Scale << "\r\n";
        sb << "   X: " << prop->Info.X << "\r\n";
        sb << "   Y: " << prop->Info.Y << "\r\n";
    }
    else {
        sb << "Direction: " << prop->Direction << "\r\n";
    }

    return sb.str();
}

std::string EntityWindow::GetPocketName(int pocket)
{
    std::string name = "";
    switch (pocket)
    {
    case 1:
        name = "Cursor";
        break;
    case 2:
        name = "Inventory";
        break;
    case 3:
        name = "Face";
        break;
    case 4:
        name = "Hair";
        break;
    case 5:
        name = "Armor";
        break;
    case 6:
        name = "Glove";
        break;
    case 7:
        name = "Shoe";
        break;
    case 8:
        name = "Head";
        break;
    case 9:
        name = "Robe";
        break;
    case 10:
        name = "R Hand 1";
        break;
    case 11:
        name = "R Hand 2";
        break;
    case 12:
        name = "L Hand 1";
        break;
    case 13:
        name = "L Hand 2";
        break;
    case 14:
        name = "Mag 1";
        break;
    case 15:
        name = "Mag 2";
        break;
    case 16:
        name = "Acc 1";
        break;
    case 17:
        name = "Acc 2";
        break;
    case 19:
        name = "Trade";
        break;
    case 20:
        name = "Temporary";
        break;
    case 23:
        name = "Quests";
        break;
    case 24:
        name = "Trash";
        break;
    case 25:
        name = "Entrustment Item 1";
        break;
    case 26:
        name = "Entrustment Item 2";
        break;
    case 27:
        name = "Entrustment Reward";
        break;
    case 28:
        name = "Battle Reward";
        break;
    case 29:
        name = "Enchant Reward";
        break;
    case 30:
        name = "Mana Crystal Reward";
        break;
    case 32:
        name = "Falias 1";
        break;
    case 33:
        name = "Falias 2";
        break;
    case 34:
        name = "Falias 3";
        break;
    case 35:
        name = "Falias 4";
        break;
    case 41:
        name = "Combo Card";
        break;
    case 43:
        name = "Armor Style";
        break;
    case 44:
        name = "Glove Style";
        break;
    case 45:
        name = "Shoe Style";
        break;
    case 46:
        name = "Head Style";
        break;
    case 47:
        name = "Robe Style";
        break;
    case 49:
        name = "Personal Inventory";
        break;
    case 50:
        name = "VIP Inventory";
        break;
    case 81:
        name = "Farm Stone";
        break;
    case 90:
        name = "Tail Style";
        break;
    case 1000:
        name = "Bard Board Scroll 1";
        break;
    case 1001:
        name = "Bard Board Scroll 2";
        break;
    case 1002:
        name = "Bard Board Scroll 3";
        break;
    case 1003:
        name = "Bard Board Scroll 4";
        break;
    case 1004:
        name = "Bard Board Scroll 5";
        break;
    case 1005:
        name = "Bard Board Scroll 6";
        break;
    case 1006:
        name = "Bard Board Scroll 7";
        break;
    case 1007:
        name = "Bard Board Scroll 8";
        break;
    case 1008:
        name = "Bard Board Scroll 9";
        break;
    case 1009:
        name = "Bard Board Scroll 10";
        break;
    case 1010:
        name = "Bard Board Scroll 11";
        break;
    case 1011:
        name = "Bard Board Scroll 12";
        break;
    case 1012:
        name = "Bard Board Scroll 13";
        break;
    case 1013:
        name = "Bard Board Scroll 14";
        break;
    case 1014:
        name = "Bard Board Scroll 15";
        break;
    case 1015:
        name = "Bard Board Scroll 16";
        break;

    default:
        if (pocket >= 100 && pocket <= 199) {
            name = "Item Bag " + std::to_string(pocket - 100 + 1);
        }
        else {
            name = "Unknown";
        }
        break;
    }

    return name;
}

std::string EntityWindow::GetConditionEngName(int conditionID) {
    static const std::array<std::string, 161> conditionNames = {
        "Poison",                        
        "Deadly",                        
        "PotionToxicosis",               
        "Paralyze",                      
        "Silence",                       
        "Stone",                         
        "Coward",                        
        "Berserk",                       
        "Confuse",                       
        "DoubleCombatExp",               
        "Slow",                          
        "Lucky",                         
        "Unlucky",                       
        "Race Enchantment",              
        "Explosive",                     
        "Explosive",                     
        "Mirage",                        
        "Weakness",                      
        "PVPPenalty",                    
        "Enervation",                    
        "Dark Knight Disarming",         
        "Stealth",                       
        "Blessed",                       
        "Transparency",                  
        "Notrade",                       
        "Follow",                        
        "NoChatting",                    
        "Cutsecne",                      
        "Ensemble",                      
        "Sharp Aiming",                  
        "Fast Casting",                  
        "Weaken",                        
        "FoodSmile",                     
        "FoodCry",                       
        "FoodCrazy",                     
        "FoodSelfPraise",                
        "FoodHeart",                     
        "PoisonImmune",                  
        "StoneImmune",                   
        "ManaSaving",                    
        "StaminaSaving",                 
        "ExplosionResistance",           
        "StompResistance",               
        "ManaWasting",                   
        "StaminaWasting",                
        "FoodSharing",                   
        "Fire Magic Shield",             
        "Ice Magic Shield",              
        "Lightning Magic Shield",        
        "Natural Magic Shield",          
        "Slow Moving",                   
        "Fast Collect",                  
        "Dash",                          
        "FastAttack",                    
        "Moonlight",                     
        "Sulfur Poison",                 
        "Burn",                          
        "Freeze",                        
        "FoodStar",                      
        "ManaShield",                    
        "CherryTreeKit",                 
        "Boost",                         
        "Fast Casting",                  
        "Boost Attack",                  
        "Half-Transparent",              
        "MoreCombatExp",                 
        "DoubleCombatExp",               
        "SkillConfusion",                
        "ElephantSprinkling",            
        "Curse",                         
        "Blind",                         
        "Freeze no effect",              
        "ProductionRateModify",          
        "ItemExpRateUp",                 
        "SkillCloudy",                   
        "FailMacroCheck",                
        "SkillSnowStorm",                
        "DoubleGore",                    
        "DemiGod",                       
        "DoubleGoreCheerful",            
        "DoubleGorePainful",             
        "ValentineHappy",                
        "ValentineUnhappy",              
        "FashionShow",                   
        "LargeUpperBody",                
        "DumbTalking",                   
        "LargeLowerBody",                
        "LargeHeight",                   
        "SmallHeight",                   
        "Blessed Effect",                
        "Berserk Effect",                
        "UseItemMiniPotion",             
        "NoPotionFood",                  
        "PythonstoneBarrier",            
        "SkillDischarge",                
        "StandEffect",                   
        "LifeSkillExpBoost",             
        "CombatSkillExpBoost",           
        "MagicSkillExpBoost",            
        "AlchemySkillExpBoost",          
        "NameColorChange",               
        "Equipped",                      
        "DemiGodStrUp",                  
        "DemiGodDexUp",                  
        "DemiGodWillUp",                 
        "DemiGodLuckUp",                 
        "DemiGodIntUp",                  
        "DemiGodFuryUp",                 
        "DemiGodSpearUp",                
        "DemiGodSpiritUp",               
        "DemiGodDuraUp",                 
        "DemiGodCooltimeDown",           
        "BrionacDmgUp",                  
        "BrionacCriticalUp",             
        "BardSongUp",                    
        "SpeedUp",                       
        "DemiGodImmune",                 
        "HeightAdjust",                  
        "RavenAttacked",                 
        "NuadhaPhaseChange",             
        "EnableAttackWithRider",         
        "StatDownCurse",                 
        "ShadowMissionBonus",            
        "EventItemDropRateUp",           
        "EventItemDropRateUp2",          
        "FishingDropBooster",            
        "FishingDropBooster2",           
        "ChattingColorChange",           
        "CombatExpBoost",                
        "DamageCurse",                   
        "Frenzy",                        
        "NuadhaSet",                     
        "StageDungeonSpotLight",         
        "PiperOfHamelin",                
        "OutOfBody",                     
        "DissolvingSoul",                
        "IceBowl",                       
        "TrollRecorvery",                
        "smash_enhance",                 
        "assaultslash_enhance",          
        "assault_enhance",               
        "icebolt_enhance",               
        "firebolt_enhance",              
        "healing_enhance",               
        "flamer_enhance",                
        "watercannon_enhance",           
        "lifedrain_enhance",             
        "magnumshot_enhance",            
        "supportshot_enhance",           
        "fishing_enhance",               
        "refine_enhance",                
        "blacksmith_enhance",            
        "metallurgy_enhance",            
        "ArmorBearRoar",                 
        "WearOpheliaGlove",              
        "PetFlyingBoost",                
        "TailingClaudius",               
        "PetTrainingKit",                
        "PetTrainingBoost",              
        "PetTrainingFlyingBoost",        
        "TodayShadowMissionComplete"     
    };

    if (conditionID >= 0 && conditionID < static_cast<int>(conditionNames.size())) {
        return conditionNames[conditionID];
    }

    return "Unknown";
}

}