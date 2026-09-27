#pragma once

#include <array>
#include <string>
#include <optional>
#include <memory>
#include <mutex>

#include <FunctionHook.hpp>

#include "Mod.hpp"

namespace kanan {
    class AutoSetMTU : public Mod {
    public:
        AutoSetMTU();

        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        bool m_isEnabled;
        std::array<char, 100> m_interface;
        int m_lowMTU;
        int m_normalMTU;
        std::unique_ptr<FunctionHook> m_hook;

        // Keeps each connection's lowering and restoring of the MTU in order.
        std::mutex m_netshMutex;
        // Counts the connections the MTU was lowered for.
        int m_connection{ 0 };

        std::optional<DWORD> runProcess(const std::string& name, const std::string& params);

        static char __cdecl createConnection(int a1, int a2);
    };
}
