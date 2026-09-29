#include <MinHook.h>

#include "FunctionHook.hpp"

using namespace std;

namespace kanan {
    bool g_isMinHookInitialized{ false };
    // Per thread: only the startup thread defers; hooks the game's thread makes meanwhile (like
    // D3D9Hook's for a changed Reset) are turned on at once.
    static thread_local bool g_isDeferringEnabling{ false };

    void FunctionHook::setDeferEnabling(bool isDeferring) {
        g_isDeferringEnabling = isDeferring;
    }

    void FunctionHook::enableDeferred() {
        // MinHook turns them on with the game's other threads suspended, like a single hook.
        MH_ApplyQueued();
    }

    FunctionHook::FunctionHook(uintptr_t target, uintptr_t destination)
        : m_target{ 0 },
        m_destination{ 0 },
        m_original{ 0 }
    {
        // Initialize MinHook if it hasn't been already.
        if (!g_isMinHookInitialized && MH_Initialize() == MH_OK) {
            g_isMinHookInitialized = true;
        }

        // Create the hook, then enable it (or queue it to be enabled). The members are set before
        // it's enabled: once it is, it can be called at once, and it may need the original.
        uintptr_t original{ 0 };

        if (MH_CreateHook((LPVOID)target, (LPVOID)destination, (LPVOID*)&original) != MH_OK) {
            return;
        }

        m_target = target;
        m_destination = destination;
        m_original = original;

        auto status = g_isDeferringEnabling ? MH_QueueEnableHook((LPVOID)target) : MH_EnableHook((LPVOID)target);

        if (status != MH_OK) {
            MH_RemoveHook((LPVOID)target);
            m_target = 0;
            m_destination = 0;
            m_original = 0;
        }
    }

    FunctionHook::~FunctionHook() {
        remove();
    }

    bool FunctionHook::remove() {
        // Don't try to remove invalid hooks.
        if (m_original == 0) {
            return true;
        }

        // Disable then remove the hook. It can be disabled already: made while enabling was
        // deferred, and removed before it was turned on (removing it also takes it off the queue).
        auto status = MH_DisableHook((LPVOID)m_target);

        if ((status != MH_OK && status != MH_ERROR_DISABLED) || MH_RemoveHook((LPVOID)m_target) != MH_OK) {
            return false;
        }

        // Invalidate the members.
        m_target = 0;
        m_destination = 0;
        m_original = 0;

        return true;
    }
}
