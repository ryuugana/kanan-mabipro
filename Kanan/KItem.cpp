#include <String.hpp>

#include "KItem.hpp"

using namespace std;

namespace kanan {
    optional<uint64_t> KItem::getID() const {
        if (entityID == nullptr) {
            return {};
        }

        return *entityID;
    }

    optional<std::string> KItem::getName() const {
        if (name == nullptr) {
            return {};
        }

        return narrow((wchar_t*)&name->buffer[0]);
    }

    optional<uint16_t> KItem::getMaxStackCount() const {
        if (dbDesc == nullptr) {
            return {};
        }

        return dbDesc->maxStackCount;
    }

    std::string KItem::GetPocketName(int pocket)
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
            name = "Right Hand 1";
            break;
        case 11:
            name = "Right Hand 2";
            break;
        case 12:
            name = "Left hand 1";
            break;
        case 13:
            name = "Left hand 2";
            break;
        case 14:
            name = "Magazine 1";
            break;
        case 15:
            name = "Magazine 2";
            break;
        case 16:
            name = "Accessory 1";
            break;
        case 17:
            name = "Accessory 2";
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
}
