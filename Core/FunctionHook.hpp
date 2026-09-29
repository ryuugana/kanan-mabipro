#pragma once

#include <cstdint>

namespace kanan {
    class FunctionHook {
    public:
        FunctionHook() = delete;
        FunctionHook(const FunctionHook& other) = delete;
        FunctionHook(FunctionHook&& other) = delete;
        FunctionHook(uintptr_t target, uintptr_t destination);
        virtual ~FunctionHook();

        // Called automatically by the destructor, but you can call it explicitly
        // if you need to remove the hook.
        bool remove();

        // Kanan loads on its own thread while the game is already running, so a hook turned on as
        // soon as it's made can be called by the game before the code that made it has kept it
        // (m_hook = make_unique<FunctionHook>(...)), when the hook's function finds no original to
        // call. While deferring, hooks are made but not turned on until enableDeferred, which Kanan
        // calls as soon as whatever made them (a mod, the D3D9 hook) is done setting up.
        static void setDeferEnabling(bool isDeferring);
        static void enableDeferred();

        auto getTarget() const {
            return m_target;
        }

        auto getOriginal() const {
            return m_original;
        }

        auto isValid() const {
            return m_original != 0;
        }

        FunctionHook& operator=(const FunctionHook& other) = delete;
        FunctionHook& operator=(FunctionHook&& other) = delete;

    private:
        uintptr_t m_target;
        uintptr_t m_destination;
        uintptr_t m_original;
    };
}
