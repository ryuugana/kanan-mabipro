// Prop.h
#pragma once
#include "IEntity.hpp"
#include <string>
#include <sstream>
#include <iomanip>

namespace kanan
{
    
struct PropInfo {
    float Altitude = 0.0f;
    uint32_t Color1 = 0;
    uint32_t Color2 = 0;
    uint32_t Color3 = 0;
    uint32_t Color4 = 0;
    uint32_t Color5 = 0;
    uint32_t Color6 = 0;
    uint32_t Color7 = 0;
    uint32_t Color8 = 0;
    uint32_t Color9 = 0;
    float Direction = 0.0f;
    int FixedAltitude = 0;
    int Id = 0;
    int Region = 0;
    float Scale = 0.0f;
    float X = 0.0f;
    float Y = 0.0f;

    bool operator==(const PropInfo& o) const {
        return Altitude == o.Altitude && Color1 == o.Color1 && Color2 == o.Color2 &&
            Color3 == o.Color3 && Color4 == o.Color4 && Color5 == o.Color5 &&
            Color6 == o.Color6 && Color7 == o.Color7 && Color8 == o.Color8 &&
            Color9 == o.Color9 && Direction == o.Direction && FixedAltitude == o.FixedAltitude &&
            Id == o.Id && Region == o.Region && Scale == o.Scale && X == o.X && Y == o.Y;
    }
};

class Prop : public IEntity {
public:
    long long EntityId = 0;
    int Id = 0;
    std::string State;
    std::string Xml;

    // Server only
    std::string Name;
    std::string Title;
    PropInfo Info;

    // Client only
    float Direction = 0.0f;

    long long GetEntityId() const override { return EntityId; }
    std::string GetName() const override { return Name; }
    std::string GetEntityType() const override { return "Prop"; }

    bool IsServerProp() const {
        constexpr long long ServerPropsThreshold = 0x4000000000000000LL; // Example MabiId threshold
        return EntityId >= ServerPropsThreshold;
    }

    std::string GetInfo() const override;
    bool Equals(const IEntity* other) const override;
};

}