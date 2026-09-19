// Creature.cpp
#include "Creature.hpp"

#include "KItem.hpp"

namespace kanan {

bool Creature::Equals(const IEntity* obj) const {
    const auto* other = dynamic_cast<const Creature*>(obj);
    if (!other) return false;

    return (this->EntityId == other->EntityId) ;
}

}