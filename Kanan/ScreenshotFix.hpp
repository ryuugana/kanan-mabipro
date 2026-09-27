#pragma once

#include <memory>
#include <string>
#include <vector>

#include <d3d9.h>

#include <FunctionHook.hpp>

#include "Mod.hpp"

namespace kanan {
    // Fixes the game's screenshots not being saved with Windows display scaling above 100% (or the
    // DPI override set to System). The game copies the screen into an image the size Windows tells
    // it the screen is; with scaling, that's smaller than the screen, and the copy fails. When it
    // does, the screenshot is taken from the next frame instead, exactly as the game draws it.
    class ScreenshotFix : public Mod {
    public:
        ScreenshotFix();

        void onFrame() override;
        void onFrameDrawn() override;

        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        // A screenshot to save from the next frame.
        struct Pending {
            std::wstring path;
            IDirect3DSurface9* surface;
            RECT rect;
            unsigned long quality;
        };

        bool m_isEnabled;
        bool m_isHookTried;

        std::unique_ptr<FunctionHook> m_hook;

        // The image the game's last failed screen copy was filled in for (not referenced).
        IDirect3DSurface9* m_scaledImage;
        std::vector<Pending> m_pending;

        bool onWrite(const std::wstring& path, IDirect3DSurface9* surface, const RECT* rect, unsigned long quality);
        bool fillFromFrame(IDirect3DDevice9* device, const Pending& screenshot);

        static HRESULT WINAPI onScreenCopy(IDirect3DDevice9* device, UINT swapChain, IDirect3DSurface9* surface);
    };
}
