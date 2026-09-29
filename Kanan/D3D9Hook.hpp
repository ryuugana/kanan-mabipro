#pragma once

#include <functional>
#include <memory>

#include <d3d9.h>

#include <FunctionHook.hpp>

namespace kanan {
    class D3D9Hook {
    public:
        //
        // Callbacks to actually do work when the hooked functions get called.
        //
        std::function<void(D3D9Hook&)> onPresent;
        std::function<void(D3D9Hook&)> onPreReset;
        std::function<void(D3D9Hook&)> onPostReset;

        D3D9Hook();
        D3D9Hook(const D3D9Hook& other) = delete;
        D3D9Hook(D3D9Hook&& other) = delete;
        virtual ~D3D9Hook();

        auto getDevice() const {
            return m_device;
        }

        auto isValid() const {
            return m_presentHook->isValid() && m_resetHook->isValid();
        }

        const auto& getPresentHook() const {
            return *m_presentHook;
        }

        const auto& getResetHook() const {
            return *m_resetHook;
        }

        // Whether the game's device resets through a function Kanan hooked: the Reset of the device
        // Kanan made to find it, or the game device's own when that's a different one.
        bool isResetHooked(uintptr_t reset) const;

        D3D9Hook& operator=(const D3D9Hook& other) = delete;
        D3D9Hook& operator=(D3D9Hook&& other) = delete;

    private:
        IDirect3DDevice9* m_device;

        std::unique_ptr<FunctionHook> m_presentHook;
        std::unique_ptr<FunctionHook> m_resetHook;

        // The game device's own Reset, when it isn't the one hooked above. Kanan finds the methods
        // on a null reference device it makes, and on some PCs the game's device resets through a
        // different function: something routes its Reset elsewhere and can swap it again seconds
        // after the game starts. Without this, Kanan's objects aren't released before the game
        // resets the device, the reset fails, and the screen stays grey while the game runs.
        // Checked every frame; a few slots in case it changes more than once.
        static constexpr size_t DEVICE_RESET_SLOTS = 3;
        std::unique_ptr<FunctionHook> m_deviceResetHooks[DEVICE_RESET_SLOTS];
        uintptr_t m_lastDeviceReset;

        bool hook();
        void hookDeviceReset(IDirect3DDevice9* device);

        static HRESULT WINAPI present(IDirect3DDevice9* device, CONST RECT* src, CONST RECT* dest, HWND wnd, CONST RGNDATA* dirtyRgn);
        static HRESULT WINAPI reset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* presentParams);
        template <size_t slot>
        static HRESULT WINAPI deviceReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* presentParams);
        static HRESULT callReset(const FunctionHook& hook, IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* presentParams);
    };
}
