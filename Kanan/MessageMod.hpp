#pragma once
#pragma once

#include <string>
#include <vector>

#include <json.hpp>
#include "Mod.hpp"

namespace kanan {
    struct MabiMessage {
        unsigned char* buffer;
        LONG			size;
    };

    // Mod made specifically for MabiMessageHook.
    class MessageMod : public Mod {
    public:
		virtual void onRecv(MabiMessage mabiMessage) { return; };

        std::vector<unsigned long>     getOp()     { return m_op; };
        bool            m_isEnabled;

    protected:
        std::vector<unsigned long>     m_op;
    };
}
