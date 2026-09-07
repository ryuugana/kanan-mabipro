// Prop.cpp
#include "Prop.hpp"

namespace kanan {

std::string Prop::GetInfo() const {
    std::ostringstream sb;
    sb << (IsServerProp() ? "Server" : "Client") << " sided prop\r\n\r\n";

    sb << "Entity id: " << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << EntityId << "\r\n";
    sb << "Prop id: " << std::dec << Id << "\r\n";
    sb << "State: " << State << "\r\n";
    sb << "XML: " << Xml << "\r\n";

    if (IsServerProp()) {
        sb << "Name: " << Name << "\r\n";
        sb << "Title: " << Title << "\r\n";
        sb << "Info: \r\n";
        sb << "   Altitude: " << Info.Altitude << "\r\n";
        sb << "   Color1: 0x" << std::hex << std::setfill('0') << std::setw(8) << Info.Color1 << "\r\n";
        sb << "   Color2: 0x" << std::setfill('0') << std::setw(8) << Info.Color2 << "\r\n";
        sb << "   Color3: 0x" << std::setfill('0') << std::setw(8) << Info.Color3 << "\r\n";
        sb << "   Color4: 0x" << std::setfill('0') << std::setw(8) << Info.Color4 << "\r\n";
        sb << "   Color5: 0x" << std::setfill('0') << std::setw(8) << Info.Color5 << "\r\n";
        sb << "   Color6: 0x" << std::setfill('0') << std::setw(8) << Info.Color6 << "\r\n";
        sb << "   Color7: 0x" << std::setfill('0') << std::setw(8) << Info.Color7 << "\r\n";
        sb << "   Color8: 0x" << std::setfill('0') << std::setw(8) << Info.Color8 << "\r\n";
        sb << "   Color9: 0x" << std::setfill('0') << std::setw(8) << Info.Color9 << "\r\n";
        sb << "   Direction: " << std::dec << Info.Direction << "\r\n";
        sb << "   FixedAltitude: " << Info.FixedAltitude << "\r\n";
        sb << "   Id: " << Info.Id << "\r\n";
        sb << "   Region: " << Info.Region << "\r\n";
        sb << "   Scale: " << Info.Scale << "\r\n";
        sb << "   X: " << Info.X << "\r\n";
        sb << "   Y: " << Info.Y << "\r\n";
    }
    else {
        sb << "Direction: " << Direction << "\r\n";
    }

    return sb.str();
}

bool Prop::Equals(const IEntity* obj) const {
    const auto* other = dynamic_cast<const Prop*>(obj);
    if (!other) return false;

    return (this->EntityId == other->EntityId) &&
        (this->Id == other->Id) &&
        (this->State == other->State) &&
        (this->Xml == other->Xml) &&
        (this->Name == other->Name) &&
        (this->Title == other->Title) &&
        (this->Info == other->Info);
}

}