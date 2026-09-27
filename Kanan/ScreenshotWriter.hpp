#pragma once

#include <functional>
#include <memory>
#include <string>

#include <d3d9.h>

#include <FunctionHook.hpp>

namespace kanan {
    // The game saves its screenshots with EXL's JPEG writer. It can only be hooked once, so the
    // screenshot mods share this hook.
    class ScreenshotWriter {
    public:
        // Called before the game saves a JPEG; returning true means it has been taken care of (and
        // is saved later), and the game is told it was saved.
        std::function<bool(const std::wstring& path, IDirect3DSurface9* surface, const RECT* rect, unsigned long quality)> onWrite;
        // Called after a JPEG is saved.
        std::function<void(const std::wstring& path, IDirect3DSurface9* surface, const RECT* rect)> onWritten;

        static ScreenshotWriter& get();

        // Whether a path is one of the game's screenshots, <folder>/<prefix><date><3-digit number>.jpg.
        // The game saves other JPEGs the same way, some of which it reads back right away.
        static bool isScreenshot(const std::wstring& path);

        bool isHooked() const {
            return m_hook && m_hook->isValid();
        }

        // Saves a JPEG the way the game does; returns 1 when saved.
        int write(const std::wstring& path, IDirect3DSurface9* surface, const RECT* rect, unsigned long quality);

    private:
        std::unique_ptr<FunctionHook> m_hook;

        ScreenshotWriter();

        static int __cdecl onWriteJpeg(const void* path, IDirect3DSurface9* surface, const RECT* rect, unsigned long quality);
    };
}
