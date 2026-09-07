// Creature.cpp
#include "Creature.hpp"

#include "KItem.hpp"

namespace kanan {

std::string Creature::GetInfo() const {
    std::ostringstream sb;
    sb << std::fixed << std::setprecision(2);

    float h = (Height < 1.0f && Height > 0.999f) ? 1.0f : Height;
    float w = (Weight < 1.0f && Weight > 0.999f) ? 1.0f : Weight;
    float u = (Upper < 1.0f && Upper > 0.999f) ? 1.0f : Upper;
    float l = (Lower < 1.0f && Lower > 0.999f) ? 1.0f : Lower;

    sb << "Entity id: " << std::hex << std::uppercase << std::setfill('0') << std::setw(16) << EntityId << "\r\n";
    sb << "Name: " << Name << "\r\n";
    sb << "Race: " << std::dec << Race << "\r\n\r\n";

    sb << "CP: " << CombatPower << "\r\n";
    sb << "Life: " << GetLife() << " (" << LifeRaw << ") / " << GetLifeMax() << " (" << LifeMaxBase << ")\r\n\r\n";

    sb << "Region: " << Region << "\r\n";
    sb << "Position: " << X << " / " << Y << "\r\n";
    sb << "Direction: " << (int)Direction << "\r\n\r\n";

    sb << "Skin color: " << (int)SkinColor << "\r\n";
    sb << "Eye type: " << EyeType << "\r\n";
    sb << "Eye color: " << (int)EyeColor << "\r\n";
    sb << "Mouth type: " << (int)MouthType << "\r\n\r\n";

    sb << "Title: " << Title << "\r\n";
    sb << "Option title: " << OptionTitle << "\r\n\r\n";

    sb << "Height: " << h << "\r\n";
    sb << "Weight: " << w << "\r\n";
    sb << "Upper:  " << u << "\r\n";
    sb << "Lower:  " << l << "\r\n\r\n";

    sb << "Color 1: 0x" << std::hex << std::setfill('0') << std::setw(8) << Color1 << "\r\n";
    sb << "Color 2: 0x" << std::setfill('0') << std::setw(8) << Color2 << "\r\n";
    sb << "Color 3: 0x" << std::setfill('0') << std::setw(8) << Color3 << "\r\n\r\n";

    sb << "Stand style: " << StandStyle << "\r\n\r\n";

    sb << "Equipped items: (Pocket, Class, Color1, Color2, Color3)\r\n";
    for (const auto& [id, item] : Items) {
        sb << std::dec << KItem::GetPocketName(item.Pocket).c_str() << ", " << item.Id;
        sb << ", 0x" << std::hex << std::setfill('0') << std::setw(8) << item.Color1 << ", 0x"
            << std::setfill('0') << std::setw(8) << item.Color2 << ", 0x"
            << std::setfill('0') << std::setw(8) << item.Color3 << "\r\n";
    }

    return sb.str();
}

bool Creature::Equals(const IEntity* obj) const {
    const auto* other = dynamic_cast<const Creature*>(obj);
    if (!other) return false;

    return (this->EntityId == other->EntityId) &&
        (this->Title == other->Title) &&
        (this->Race == other->Race) &&
        (this->SkinColor == other->SkinColor) &&
        (this->EyeType == other->EyeType) &&
        (this->EyeColor == other->EyeColor) &&
        (this->MouthType == other->MouthType);
}

}