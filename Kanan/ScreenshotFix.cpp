#include <algorithm>
#include <cstdint>
#include <utility>

#include <Windows.h>

#include <imgui.h>

#include "Log.hpp"
#include "Kanan.hpp"
#include "ScreenshotWriter.hpp"
#include "ScreenshotFix.hpp"

using namespace std;

namespace kanan {
    static ScreenshotFix* g_screenshotFix{ nullptr };

    // IDirect3DDevice9::GetFrontBufferData: what the game copies the screen with.
    constexpr size_t getFrontBufferDataIndex = 33;

    using GetFrontBufferData = HRESULT(WINAPI*)(IDirect3DDevice9* device, UINT swapChain, IDirect3DSurface9* surface);

    // Looked up at runtime, as older versions of Windows don't have it. Makes the calling thread see
    // the screen's real size.
    using SetThreadDpiAwarenessContextFn = HANDLE(WINAPI*)(HANDLE context);
    static const auto dpiAwarenessPerMonitor = (HANDLE)-3;

    // The screen's real size, which Windows hides from the game when it scales it.
    static pair<UINT, UINT> realScreenSize() {
        auto setDpiAwareness = (SetThreadDpiAwarenessContextFn)GetProcAddress(GetModuleHandleW(L"user32.dll"),
            "SetThreadDpiAwarenessContext");

        if (setDpiAwareness == nullptr) {
            return {};
        }

        auto previous = setDpiAwareness(dpiAwarenessPerMonitor);
        pair<UINT, UINT> size{ GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };

        if (previous != nullptr) {
            setDpiAwareness(previous);
        }

        return size;
    }

    static bool isSupported(D3DFORMAT format) {
        return format == D3DFMT_A8R8G8B8 || format == D3DFMT_X8R8G8B8;
    }

    // Scales SOURCE (the real screen) down to DESTINATION (the size the game expects), averaging the
    // source pixels each destination pixel covers.
    static bool scaleInto(IDirect3DSurface9* source, UINT sourceWidth, UINT sourceHeight, IDirect3DSurface9* destination,
        UINT destinationWidth, UINT destinationHeight)
    {
        D3DLOCKED_RECT from{}, to{};

        if (FAILED(source->LockRect(&from, nullptr, D3DLOCK_READONLY))) {
            return false;
        }

        if (FAILED(destination->LockRect(&to, nullptr, 0))) {
            source->UnlockRect();
            return false;
        }

        // The source pixels (columns or rows) destination pixel I covers.
        auto span = [](UINT i, UINT sourceSize, UINT destinationSize) {
            auto first = (UINT)((uint64_t)i * sourceSize / destinationSize);
            auto last = (UINT)((uint64_t)(i + 1) * sourceSize / destinationSize);

            return pair<UINT, UINT>{ first, max(last, first + 1) };
        };

        for (UINT y = 0; y < destinationHeight; ++y) {
            auto rows = span(y, sourceHeight, destinationHeight);
            auto out = (uint32_t*)((uint8_t*)to.pBits + (size_t)y * to.Pitch);

            for (UINT x = 0; x < destinationWidth; ++x) {
                auto columns = span(x, sourceWidth, destinationWidth);
                uint32_t red{}, green{}, blue{}, count{};

                for (auto row = rows.first; row < rows.second; ++row) {
                    auto in = (const uint32_t*)((const uint8_t*)from.pBits + (size_t)row * from.Pitch);

                    for (auto column = columns.first; column < columns.second; ++column) {
                        auto pixel = in[column];

                        red += (pixel >> 16) & 0xFF;
                        green += (pixel >> 8) & 0xFF;
                        blue += pixel & 0xFF;
                        ++count;
                    }
                }

                out[x] = 0xFF000000 | (red / count) << 16 | (green / count) << 8 | (blue / count);
            }
        }

        destination->UnlockRect();
        source->UnlockRect();

        return true;
    }

    // Copies the screen at its real size and scales it into SURFACE, the size the game expects.
    static HRESULT copyScaledScreen(IDirect3DDevice9* device, UINT swapChain, IDirect3DSurface9* surface,
        const D3DSURFACE_DESC& desc, GetFrontBufferData original)
    {
        if (desc.Pool != D3DPOOL_SYSTEMMEM || !isSupported(desc.Format)) {
            return D3DERR_INVALIDCALL;
        }

        // The sizes the screen could really be: its size with scaling undone, or the display mode.
        vector<pair<UINT, UINT>> sizes{ realScreenSize() };
        D3DDISPLAYMODE mode{};

        if (SUCCEEDED(device->GetDisplayMode(swapChain, &mode))) {
            sizes.emplace_back(mode.Width, mode.Height);
        }

        vector<pair<UINT, UINT>> tried{};

        for (auto [width, height] : sizes) {
            // Only larger sizes (scaling makes the game's smaller), each once.
            if (width < desc.Width || height < desc.Height || (width == desc.Width && height == desc.Height) ||
                find(tried.begin(), tried.end(), pair<UINT, UINT>{ width, height }) != tried.end())
            {
                continue;
            }

            tried.emplace_back(width, height);

            IDirect3DSurface9* screen{ nullptr };

            if (FAILED(device->CreateOffscreenPlainSurface(width, height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &screen,
                nullptr)))
            {
                continue;
            }

            auto isCopied = SUCCEEDED(original(device, swapChain, screen)) &&
                scaleInto(screen, width, height, surface, desc.Width, desc.Height);

            screen->Release();

            if (isCopied) {
                return D3D_OK;
            }
        }

        return D3DERR_INVALIDCALL;
    }

    ScreenshotFix::ScreenshotFix()
        : m_isEnabled{ false },
        m_isHookTried{ false },
        m_hook{},
        m_scaledImage{ nullptr },
        m_pending{}
    {
        g_screenshotFix = this;

        ScreenshotWriter::get().onWrite = [this](const wstring& path, IDirect3DSurface9* surface, const RECT* rect,
            unsigned long quality)
        {
            return onWrite(path, surface, rect, quality);
        };
    }

    void ScreenshotFix::onFrame() {
        if (m_isHookTried || !m_isEnabled) {
            return;
        }

        // Hooked from the game's own device, once it has one.
        auto d3d9 = g_kanan->getD3D9Hook();
        auto device = d3d9 != nullptr ? d3d9->getDevice() : nullptr;

        if (device == nullptr) {
            return;
        }

        m_isHookTried = true;

        auto target = (*(uintptr_t**)device)[getFrontBufferDataIndex];

        m_hook = make_unique<FunctionHook>(target, (uintptr_t)&ScreenshotFix::onScreenCopy);

        log("[ScreenshotFix] %s the screen copy", m_hook->isValid() ? "Hooked" : "Failed to hook");
    }

    HRESULT WINAPI ScreenshotFix::onScreenCopy(IDirect3DDevice9* device, UINT swapChain, IDirect3DSurface9* surface) {
        auto self = g_screenshotFix;
        auto original = (GetFrontBufferData)self->m_hook->getOriginal();
        auto result = original(device, swapChain, surface);

        if (SUCCEEDED(result) || surface == nullptr || !self->m_isEnabled) {
            return result;
        }

        D3DSURFACE_DESC desc{};

        surface->GetDesc(&desc);

        // Filled right away (the game may use it right away), scaled down from the real screen. A
        // screenshot saved from it is then taken from the next frame instead, as the game draws it.
        if (FAILED(copyScaledScreen(device, swapChain, surface, desc, original))) {
            log("[ScreenshotFix] The game's screen copy failed (error %08X, %ux%u image), and so did copying it at the "
                "screen's real size", result, desc.Width, desc.Height);
            return result;
        }

        self->m_scaledImage = surface;

        return D3D_OK;
    }

    bool ScreenshotFix::onWrite(const wstring& path, IDirect3DSurface9* surface, const RECT* rect, unsigned long quality) {
        if (surface == nullptr || surface != m_scaledImage) {
            return false;
        }

        m_scaledImage = nullptr;

        // A screenshot the screen copy failed for: saved from the next frame. Anything else is saved
        // now, scaled down, as the game may read it back right away.
        if (rect == nullptr || !ScreenshotWriter::isScreenshot(path)) {
            return false;
        }

        surface->AddRef();
        m_pending.push_back({ path, surface, *rect, quality });

        return true;
    }

    void ScreenshotFix::onFrameDrawn() {
        if (m_pending.empty()) {
            return;
        }

        auto device = g_kanan->getD3D9Hook()->getDevice();

        for (auto& screenshot : m_pending) {
            // If the frame can't be used, the image keeps the scaled-down screen.
            auto isExact = device != nullptr && fillFromFrame(device, screenshot);
            auto result = ScreenshotWriter::get().write(screenshot.path, screenshot.surface, &screenshot.rect,
                screenshot.quality);

            log("[ScreenshotFix] %s a screenshot %s", result == 1 ? "Saved" : "Failed to save",
                isExact ? "from the next frame, as the game drew it" : "scaled down from the screen");

            screenshot.surface->Release();
        }

        m_pending.clear();
    }

    // Fills the part of SCREENSHOT's image (the game's image of its scaled screen) that the game saves
    // (its window) with the frame just drawn, pixel for pixel.
    bool ScreenshotFix::fillFromFrame(IDirect3DDevice9* device, const Pending& screenshot) {
        auto window = g_kanan->getWindow();
        RECT client{};
        POINT origin{};
        D3DSURFACE_DESC imageDesc{};

        if (window == nullptr || !GetClientRect(window, &client) || !ClientToScreen(window, &origin) ||
            FAILED(screenshot.surface->GetDesc(&imageDesc)) || !isSupported(imageDesc.Format))
        {
            return false;
        }

        IDirect3DSurface9* backBuffer{ nullptr };

        if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer))) {
            return false;
        }

        D3DSURFACE_DESC desc{};

        backBuffer->GetDesc(&desc);

        // The frame must be the window's drawing area, pixel for pixel.
        if (!isSupported(desc.Format) || (LONG)desc.Width != client.right || (LONG)desc.Height != client.bottom) {
            log("[ScreenshotFix] The game's frame (%ux%u) isn't its window's size (%ldx%ld)", desc.Width, desc.Height,
                client.right, client.bottom);
            backBuffer->Release();
            return false;
        }

        // Read back through a copy without multisampling, if the frame has it.
        IDirect3DSurface9* source{ backBuffer };
        IDirect3DSurface9* resolved{ nullptr };
        IDirect3DSurface9* frame{ nullptr };
        auto isRead = false;

        if (desc.MultiSampleType != D3DMULTISAMPLE_NONE) {
            if (SUCCEEDED(device->CreateRenderTarget(desc.Width, desc.Height, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE,
                &resolved, nullptr)) && SUCCEEDED(device->StretchRect(backBuffer, nullptr, resolved, nullptr, D3DTEXF_NONE)))
            {
                source = resolved;
            }
            else {
                source = nullptr;
            }
        }

        if (source != nullptr &&
            SUCCEEDED(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &frame,
                nullptr)))
        {
            isRead = SUCCEEDED(device->GetRenderTargetData(source, frame));
        }

        if (resolved != nullptr) {
            resolved->Release();
        }

        backBuffer->Release();

        D3DLOCKED_RECT from{}, to{};

        if (!isRead || FAILED(frame->LockRect(&from, nullptr, D3DLOCK_READONLY))) {
            if (frame != nullptr) {
                frame->Release();
            }

            return false;
        }

        if (FAILED(screenshot.surface->LockRect(&to, nullptr, 0))) {
            frame->UnlockRect();
            frame->Release();
            return false;
        }

        // The saved part, where it is in the image and in the frame.
        auto& rect = screenshot.rect;
        auto left = max<LONG>({ rect.left, origin.x, 0 });
        auto top = max<LONG>({ rect.top, origin.y, 0 });
        auto right = min<LONG>({ rect.right, origin.x + client.right, (LONG)imageDesc.Width });
        auto bottom = min<LONG>({ rect.bottom, origin.y + client.bottom, (LONG)imageDesc.Height });

        for (auto y = top; y < bottom; ++y) {
            auto in = (const uint32_t*)((const uint8_t*)from.pBits + (size_t)(y - origin.y) * from.Pitch);
            auto out = (uint32_t*)((uint8_t*)to.pBits + (size_t)y * to.Pitch);

            for (auto x = left; x < right; ++x) {
                out[x] = in[x - origin.x] | 0xFF000000u;
            }
        }

        screenshot.surface->UnlockRect();
        frame->UnlockRect();
        frame->Release();

        return true;
    }

    void ScreenshotFix::onUI() {
        if (ImGui::TreeNode("Screenshot Fix")) {
            ImGui::TextWrapped("For players whose screenshots aren't saved when Windows display scaling is above 100%, "
                "or when the game's DPI override is set to System. With this on, those screenshots are saved again, "
                "taken from the next frame exactly as the game draws it. Changes nothing when Windows doesn't scale "
                "the game.");
            ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
            ImGui::Checkbox("Enable Screenshot Fix", &m_isEnabled);
            ImGui::TreePop();
        }
    }

    void ScreenshotFix::onConfigLoad(const Config& cfg) {
        m_isEnabled = cfg.get<bool>("ScreenshotFix.Enabled").value_or(false);
    }

    void ScreenshotFix::onConfigSave(Config& cfg) {
        cfg.set<bool>("ScreenshotFix.Enabled", m_isEnabled);
    }
}
