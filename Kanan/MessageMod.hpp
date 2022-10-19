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
        int     getOp()     { return m_op; };
        void*   getFuncPtr(){ return m_funcPtr; };
        bool    m_isEnabled;

    protected:
        int     m_op;
        void*	m_funcPtr = nullptr;
    };
}
