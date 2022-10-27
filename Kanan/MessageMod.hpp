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
		virtual unsigned long onRecv(MabiMessage mabiMessage) { return m_op; };

        int     getOp()     { return m_op; };
        bool    m_isEnabled;

    protected:
        int     m_op;
    };
}
