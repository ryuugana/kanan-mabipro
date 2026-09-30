#include <String.hpp>

#include "Log.hpp"
#include "D3D9Hook.hpp"

using namespace std;

namespace kanan {
    static D3D9Hook* g_d3d9Hook{ nullptr };

    // How deep in Reset calls the game is: the game device's Reset can call the one Kanan hooked
    // first, and the callbacks only run for the outermost.
    static int g_resetDepth{ 0 };

    D3D9Hook::D3D9Hook()
        : onPresent{},
        onPreReset{},
        onPostReset{},
        m_device{ nullptr },
        m_presentHook{ nullptr },
        m_resetHook{ nullptr },
        m_deviceResetHooks{},
        m_lastDeviceReset{ 0 }
    {
        if (g_d3d9Hook == nullptr) {
            if (hook()) {
                log("D3D9Hook hooked successfully.");
            }
            else {
                log("D3D9Hook failed to hook.");
            }
        }
    }

    D3D9Hook::~D3D9Hook() {
        // Explicitly unhook the methods we hooked so we can reset g_d3d9Hook.
        m_presentHook.reset();
        m_resetHook.reset();

        for (auto& hook : m_deviceResetHooks) {
            hook.reset();
        }

        g_d3d9Hook = nullptr;
    }

    bool D3D9Hook::isResetHooked(uintptr_t reset) const {
        if (m_resetHook != nullptr && reset == m_resetHook->getTarget()) {
            return true;
        }

        for (auto& hook : m_deviceResetHooks) {
            if (hook != nullptr && reset == hook->getTarget()) {
                return true;
            }
        }

        return false;
    }

    // Every frame: one read, unless the game device's Reset changed since the last frame.
    void D3D9Hook::hookDeviceReset(IDirect3DDevice9* device) {
        auto reset = (*(uintptr_t**)device)[16];

        if (reset == m_lastDeviceReset) {
            return;
        }

        if (m_lastDeviceReset == 0) {
            log("Watching the game device's Reset (%p; hooked %p)", reset, m_resetHook->getTarget());
        }

        m_lastDeviceReset = reset;

        if (isResetHooked(reset)) {
            return;
        }

        using ResetFn = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
        static const uintptr_t destinations[DEVICE_RESET_SLOTS] = {
            (uintptr_t)(ResetFn)&D3D9Hook::deviceReset<0>,
            (uintptr_t)(ResetFn)&D3D9Hook::deviceReset<1>,
            (uintptr_t)(ResetFn)&D3D9Hook::deviceReset<2>,
        };

        for (size_t slot = 0; slot < DEVICE_RESET_SLOTS; ++slot) {
            auto& hook = m_deviceResetHooks[slot];

            if (hook != nullptr) {
                continue;
            }

            log("The game's device now resets with %p, not a hooked Reset; hooking it too", reset);

            hook = make_unique<FunctionHook>(reset, destinations[slot]);

            if (!hook->isValid()) {
                log("Failed to hook the game device's Reset");
                hook.reset();
            }

            return;
        }

        log("The game's device resets with %p, but Kanan has no hooks left for it", reset);
    }

    bool D3D9Hook::hook() {
        log("Entering D3D9Hook::hook().");

        // Set hook object preemptively -- otherwise, the hook is written and is likely
        // to execute and crash before we verify success.
        g_d3d9Hook = this;

        // All we do here is create a IDirect3DDevice9 so that we can get the address
        // of the methods we want to hook from its vtable.
        using D3DCreate9Fn = IDirect3D9* (WINAPI*)(UINT);

        auto d3d9 = LoadLibrary(widen("d3d9.dll").c_str());
        auto d3dCreate9 = (D3DCreate9Fn)GetProcAddress(d3d9, "Direct3DCreate9");

        if (d3dCreate9 == nullptr) {
            log("Couldn't find Direct3DCreate9.");
            return false;
        }

        log("Got Direct3DCreate9 %p", d3dCreate9);

        auto d3d = d3dCreate9(D3D_SDK_VERSION);

        if (d3d == nullptr) {
            log("Failed to create IDirect3D9.");
            return false;
        }

        log("Got IDirect3D9 %p", d3d);

        D3DPRESENT_PARAMETERS pp{};

        ZeroMemory(&pp, sizeof(pp));

        pp.Windowed = 1;
        pp.SwapEffect = D3DSWAPEFFECT_FLIP;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        pp.BackBufferCount = 1;
        pp.hDeviceWindow = GetDesktopWindow();
        pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

        IDirect3DDevice9* device{ nullptr };

        if (FAILED(d3d->CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_NULLREF,
            GetDesktopWindow(),
            D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_NOWINDOWCHANGES,
            &pp,
            &device)))
        {
            log("Failed to create IDirect3DDevice9.");
            d3d->Release();
            return false;
        }

        log("Got IDirect3DDevice9 %p", device);

        // Grab the addresses of the methods we want to hook.
        auto present = (*(uintptr_t**)device)[17];
        auto reset = (*(uintptr_t**)device)[16];

        log("Got IDirect3DDevice9::Present %p", present);
        log("Got IDirect3DDevice9::Reset %p", reset);

        device->Release();
        d3d->Release();

        // Hook them.
        m_presentHook = make_unique<FunctionHook>(present, (uintptr_t)&D3D9Hook::present);
        m_resetHook = make_unique<FunctionHook>(reset, (uintptr_t)&D3D9Hook::reset);

        if (m_presentHook->isValid() && m_resetHook->isValid()) {
            return true;
        }
        else {
            // If a problem occurred, reset the hook.
            m_presentHook.reset();
            m_resetHook.reset();
            g_d3d9Hook = nullptr;
            return false;
        }
    }

    HRESULT D3D9Hook::present(IDirect3DDevice9* device, CONST RECT* src, CONST RECT* dest, HWND wnd, CONST RGNDATA* dirtyRgn) {
        auto d3d9 = g_d3d9Hook;

        d3d9->m_device = device;

        // Make sure a reset of the game's own device goes through Kanan, even if its Reset changes.
        d3d9->hookDeviceReset(device);

        // Call our present callback.
        if (d3d9->onPresent) {
            d3d9->onPresent(*d3d9);
        }

        // Call the original present.
        auto originalPresent = (decltype(D3D9Hook::present)*)d3d9->m_presentHook->getOriginal();

        return originalPresent(device, src, dest, wnd, dirtyRgn);
    }

    HRESULT D3D9Hook::reset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* presentParams) {
        return callReset(*g_d3d9Hook->m_resetHook, device, presentParams);
    }

    template <size_t slot>
    HRESULT D3D9Hook::deviceReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* presentParams) {
        return callReset(*g_d3d9Hook->m_deviceResetHooks[slot], device, presentParams);
    }

    HRESULT D3D9Hook::callReset(const FunctionHook& hook, IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* presentParams) {
        auto d3d9 = g_d3d9Hook;
        auto isOutermost = g_resetDepth == 0;

        d3d9->m_device = device;

        // Call our pre reset callback.
        if (isOutermost && d3d9->onPreReset) {
            d3d9->onPreReset(*d3d9);
        }

        // Call the original reset.
        auto originalReset = (decltype(D3D9Hook::reset)*)hook.getOriginal();

        ++g_resetDepth;
        auto result = originalReset(device, presentParams);
        --g_resetDepth;

        // Call our post reset callback.
        if (isOutermost && result == D3D_OK && d3d9->onPostReset) {
            d3d9->onPostReset(*d3d9);
        }

        return result;
    }
}
