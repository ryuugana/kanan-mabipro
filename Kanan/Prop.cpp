// Prop.cpp
#include "Prop.hpp"

namespace kanan {

bool Prop::Equals(const IEntity* obj) const {
    const auto* other = dynamic_cast<const Prop*>(obj);
    if (!other) return false;

    return (this->EntityId == other->EntityId);
}

}