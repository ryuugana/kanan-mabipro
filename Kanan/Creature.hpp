// Creature.h
#pragma once
#include "IEntity.hpp"
#include <string>
#include <unordered_map>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <regex>

namespace kanan {
// Placeholder struct for ItemInfo matching MabiLib
struct ItemInfo {
    int Pocket;
    int Id;
    uint32_t Color1;
    uint32_t Color2;
    uint32_t Color3;
    unsigned short Amount;
    short __unknown7;
    int Region;
    int X;
    int Y;

    /// <summary>
    /// State of the item? (eg. hoods and helmets)
    /// Part of giant's beards
    /// </summary>
    uint8_t State; // FigureA

    /// <summary>
    /// - Ego aura level (0-21)
    /// - Related to giant's beards
    /// </summary>
    uint8_t FigureB;

    /// <summary>
    /// Direction? (in region)
    /// </summary>
    uint8_t FigureC;

    uint8_t FigureD;
    uint8_t KnockCount;
    uint8_t __unknown12;
    uint8_t __unknown13;
    uint8_t __unknown14;
};

class Creature : public IEntity {
public:
    long long EntityId = 0;
    uint8_t Type = 0;
    std::string Name;
    int Race = 0;

    uint8_t SkinColor = 0;
    int EyeType = 0;
    uint8_t EyeColor = 0;
    uint8_t MouthType = 0;

    uint32_t State = 0;
    uint32_t StateEx = 0;
    uint32_t StateEx2 = 0;

    float Height = 0.0f;
    float Weight = 0.0f;
    float Upper = 0.0f;
    float Lower = 0.0f;

    int Region = 0;
    int X = 0;
    int Y = 0;

    uint8_t Direction = 0;
    int BattleState = 0;
    uint8_t WeaponSet = 0;

    uint32_t Color1 = 0;
    uint32_t Color2 = 0;
    uint32_t Color3 = 0;

    float CombatPower = 0.0f;
    std::string StandStyle;

    float LifeRaw = 0.0f;
    float LifeMaxBase = 0.0f;
    float LifeMaxMod = 0.0f;
    float LifeInjured = 0.0f;

    int Title = 0;
    uint64_t TitleApplied = 0; // DateTime stored as timestamp/ticks
    int OptionTitle = 0;

    std::string MateName;
    uint8_t Destiny = 0;

    std::unordered_map<long long, ItemInfo> Items;

    // Interface Implementations
    long long GetEntityId() const override { return EntityId; }
    std::string GetName() const override { return Name; }

    bool IsMonster() const { constexpr uint64_t MOB_MASK = 0x0010F00000000000; return (EntityId & MOB_MASK) == MOB_MASK;  }
    bool IsNpc() const { return !Name.empty() && Name[0] == '_'; }
    bool IsPet() const { constexpr uint64_t PET_MASK = 0x0010010000000000; return (EntityId & PET_MASK) == PET_MASK; }

    float GetLifeMax() const { return LifeMaxBase + LifeMaxMod; }
    float GetLife() const { return (std::min)(GetLifeMax(), LifeRaw); }

    std::string GetEntityType() const override {
        if (IsNpc()) return "NPC";
        else if (IsMonster()) return "Monster";
        else if (IsPet()) return "Pet";
        else return "Player";
    }

    std::string GetInfo() const override;
    bool Equals(const IEntity* other) const override;
};

}