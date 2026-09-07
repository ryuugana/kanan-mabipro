#pragma once
#include <string>
#include <memory>

namespace kanan {

class IEntity {
public:
    virtual ~IEntity() = default;

    virtual long long GetEntityId() const = 0;
    virtual std::string GetEntityType() const = 0;
    virtual std::string GetName() const = 0;

    virtual std::string GetInfo() const = 0;
    virtual bool Equals(const IEntity* other) const = 0;
};

}