#pragma once

#include <memory>

#include <FunctionHook.hpp>

#include "Mod.hpp"

namespace kanan {
    class DisableNagle : public Mod {
    public:
        DisableNagle();

        std::string getName() override { return "Disable Nagle"; }

        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        bool m_isEnabled;
        std::unique_ptr<FunctionHook> m_hook;

        static UINT_PTR __stdcall socket(int af, int type, int protocol);
    };
}
