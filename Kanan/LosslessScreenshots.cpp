#pragma comment(lib, "windowscodecs")

#include <algorithm>
#include <cstdint>
#include <thread>
#include <vector>

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <imgui.h>

#include "Log.hpp"
#include "ScreenshotWriter.hpp"
#include "LosslessScreenshots.hpp"

using namespace std;
using Microsoft::WRL::ComPtr;

namespace kanan {
    // Writes 24-bit BGR PIXELS as a PNG with Windows' encoder.
    static bool writePng(const wstring& path, UINT width, UINT height, vector<uint8_t>& pixels) {
        ComPtr<IWICImagingFactory> factory{};
        ComPtr<IWICStream> stream{};
        ComPtr<IWICBitmapEncoder> encoder{};
        ComPtr<IWICBitmapFrameEncode> frame{};
        ComPtr<IPropertyBag2> properties{};
        auto format = GUID_WICPixelFormat24bppBGR;

        return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
            SUCCEEDED(factory->CreateStream(&stream)) &&
            SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
            SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
            SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
            SUCCEEDED(encoder->CreateNewFrame(&frame, &properties)) &&
            SUCCEEDED(frame->Initialize(properties.Get())) &&
            SUCCEEDED(frame->SetSize(width, height)) &&
            SUCCEEDED(frame->SetPixelFormat(&format)) && format == GUID_WICPixelFormat24bppBGR &&
            SUCCEEDED(frame->WritePixels(height, width * 3, (UINT)pixels.size(), pixels.data())) &&
            SUCCEEDED(frame->Commit()) &&
            SUCCEEDED(encoder->Commit());
    }

    LosslessScreenshots::LosslessScreenshots()
        : m_isEnabled{ false },
        m_saved{ 0 },
        m_failed{ 0 }
    {
        ScreenshotWriter::get().onWritten = [this](const wstring& path, IDirect3DSurface9* surface, const RECT* rect) {
            if (m_isEnabled && surface != nullptr && ScreenshotWriter::isScreenshot(path)) {
                savePng(path, surface, rect);
            }
        };
    }

    void LosslessScreenshots::onFrame() {
        if (auto saved = m_saved.exchange(0)) {
            log("[LosslessScreenshots] Saved %u PNG screenshot(s)", saved);
        }

        if (auto failed = m_failed.exchange(0)) {
            log("[LosslessScreenshots] Failed to save %u PNG screenshot(s)", failed);
        }
    }

    void LosslessScreenshots::savePng(const wstring& path, IDirect3DSurface9* surface, const RECT* rect) {
        D3DSURFACE_DESC desc{};

        if (FAILED(surface->GetDesc(&desc)) || (desc.Format != D3DFMT_A8R8G8B8 && desc.Format != D3DFMT_X8R8G8B8)) {
            ++m_failed;
            return;
        }

        // The part of the image the game saved (its window).
        auto left = rect ? max<LONG>(rect->left, 0) : 0;
        auto top = rect ? max<LONG>(rect->top, 0) : 0;
        auto right = rect ? min<LONG>(rect->right, (LONG)desc.Width) : (LONG)desc.Width;
        auto bottom = rect ? min<LONG>(rect->bottom, (LONG)desc.Height) : (LONG)desc.Height;

        D3DLOCKED_RECT locked{};

        if (right <= left || bottom <= top || FAILED(surface->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
            ++m_failed;
            return;
        }

        // Copied now, while the game still has the image; encoded in the background.
        auto width = (UINT)(right - left);
        auto height = (UINT)(bottom - top);
        vector<uint8_t> pixels((size_t)width * height * 3);

        for (UINT y = 0; y < height; ++y) {
            auto in = (const uint32_t*)((const uint8_t*)locked.pBits + (size_t)(top + y) * locked.Pitch) + left;
            auto out = &pixels[(size_t)y * width * 3];

            for (UINT x = 0; x < width; ++x) {
                out[x * 3] = in[x] & 0xFF;
                out[x * 3 + 1] = (in[x] >> 8) & 0xFF;
                out[x * 3 + 2] = (in[x] >> 16) & 0xFF;
            }
        }

        surface->UnlockRect();

        auto png = path.substr(0, path.size() - 4) + L".png";

        thread{ [this, png, width, height, pixels = move(pixels)]() mutable {
            auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

            if (writePng(png, width, height, pixels)) {
                ++m_saved;
            }
            else {
                ++m_failed;
            }

            if (SUCCEEDED(com)) {
                CoUninitialize();
            }
        } }.detach();
    }

    void LosslessScreenshots::onUI() {
        if (!ScreenshotWriter::get().isHooked()) {
            return;
        }

        if (ImGui::TreeNode("Lossless Screenshots")) {
            ImGui::TextWrapped("Also saves each screenshot as a PNG, next to the game's JPEG. PNGs are lossless: exactly "
                "what the game drew, without the JPEG's compression artifacts, but several times larger. The JPEG is "
                "kept, as the game uses it.");
            ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
            ImGui::Checkbox("Enable Lossless Screenshots", &m_isEnabled);
            ImGui::TreePop();
        }
    }

    void LosslessScreenshots::onConfigLoad(const Config& cfg) {
        m_isEnabled = cfg.get<bool>("LosslessScreenshots.Enabled").value_or(false);
    }

    void LosslessScreenshots::onConfigSave(Config& cfg) {
        cfg.set<bool>("LosslessScreenshots.Enabled", m_isEnabled);
    }
}
