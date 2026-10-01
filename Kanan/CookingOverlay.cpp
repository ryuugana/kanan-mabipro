#include "CookingOverlay.hpp"
#include "Kanan.hpp"
#include "Log.hpp"
#include "resource.h"

#include <Windows.h>
#include <vector>
#include <algorithm> // for clamp

using namespace kanan;

CookingOverlay::CookingOverlay()
    : m_isEnabled(false),
    m_texture(nullptr),
    m_w(0),
    m_h(0),
    m_scale(1.0f)
{
}

CookingOverlay::~CookingOverlay() {
    releaseTexture();
}

void CookingOverlay::onUI() {
    if (ImGui::TreeNode(getName().c_str())) {
        ImGui::TextWrapped("Displays a cooking ruler in a transparent window.");
        ImGui::Dummy(ImVec2{ 5.0f, 5.0f });

        ImGui::Checkbox("Enable Cooking Overlay", &m_isEnabled);

        // Scale control: 1.0 = original size. Allow some reasonable range.
        ImGui::Spacing();
        ImGui::Text("Scale:");
        if (ImGui::InputFloat("##CookingOverlayScale", &m_scale, 0.05f, 0.25f, "%.2f")) {
            // clamp to avoid zero/negative sizes
            m_scale = std::max(0.05f, m_scale);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            m_scale = 1.0f;
        }

        ImGui::TreePop();
    }
}

bool CookingOverlay::onWindow() {
    if (!m_isEnabled) {
        // ensure we don't keep GPU resources when disabled
        if (m_texture) {
            releaseTexture();
        }
        return false;
    }

    // Try to create texture if needed
    ensureTexture();

    drawWindow();

    return m_isEnabled;
}

void CookingOverlay::drawWindow() {
    // Transparent background
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));

    // Compute scaled size
    const float baseW = (m_w > 0) ? static_cast<float>(m_w) : 256.0f;
    const float baseH = (m_h > 0) ? static_cast<float>(m_h) : 256.0f;
    const float scaledW = baseW * m_scale;
    const float scaledH = baseH * m_scale;

    // Set the window size slightly larger to the scaled texture size so it fits.
    ImGui::SetNextWindowSize(ImVec2{ scaledW + 15, scaledH + 20}, ImGuiCond_Always);

    // No title bar/resize to resemble an overlay
    if (!ImGui::Begin("CookingOverlay", &m_isEnabled,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        ImGui::PopStyleColor();
        return;
    }

    if (m_texture) {
        ImGui::Image((ImTextureID)m_texture, ImVec2{ scaledW, scaledH });
    }
    else {
        ImGui::Text("Loading cooking overlay...");
    }

    ImGui::End();
    ImGui::PopStyleColor();
}

bool CookingOverlay::ensureTexture() {
    // If texture already exists and device is valid, nothing to do
    if (m_texture) {
        return true;
    }

    // Get the D3D device from Kanan's D3D9Hook
    auto d3dHook = g_kanan->getD3D9Hook();
    if (!d3dHook) {
        return false;
    }

    auto device = d3dHook->getDevice();
    if (device == nullptr) {
        return false;
    }

    // Create texture from bitmap resource
    try {
        createTextureFromResource();
    }
    catch (...) {
        // If creation fails, ensure texture is null
        releaseTexture();
        return false;
    }

    return m_texture != nullptr;
}

void CookingOverlay::createTextureFromResource() {
    // Load HBITMAP from module resources (expects the bitmap compiled as a resource)
    HMODULE hmod = g_kanan->getHModule();
    if (hmod == nullptr) {
        return;
    }

    // Use LoadImage with LR_CREATEDIBSECTION to get a DIB section for easy access
    HBITMAP hBmp = (HBITMAP)LoadImage(hmod, MAKEINTRESOURCE(IDB_BITMAP1), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    if (hBmp == nullptr) {
        log("[CookingOverlay] Failed to LoadImage resource IDB_BITMAP1");
        return;
    }

    BITMAP bm{};
    if (GetObject(hBmp, sizeof(BITMAP), &bm) == 0) {
        log("[CookingOverlay] GetObject failed for HBITMAP");
        DeleteObject(hBmp);
        return;
    }

    const int width = bm.bmWidth;
    const int height = bm.bmHeight;

    // Prepare BITMAPINFO to retrieve 32-bit BGRA pixels (top-down)
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    // negative height -> top-down DIB (so rows are in top-to-bottom order)
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    std::vector<uint8_t> pixels;
    pixels.resize(width * height * 4);

    HDC screenDC = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(screenDC);
    HGDIOBJ oldBmp = SelectObject(memDC, hBmp);

    // GetDIBits will fill the pixels buffer with BGRA (B G R A) — A may be zero for BMPs
    if (GetDIBits(memDC, hBmp, 0, height, pixels.data(), &bi, DIB_RGB_COLORS) == 0) {
        log("[CookingOverlay] GetDIBits failed");
        SelectObject(memDC, oldBmp);
        DeleteDC(memDC);
        ReleaseDC(NULL, screenDC);
        DeleteObject(hBmp);
        return;
    }

    SelectObject(memDC, oldBmp);
    DeleteDC(memDC);
    ReleaseDC(NULL, screenDC);
    // we can delete the HBITMAP now
    DeleteObject(hBmp);

    // Create D3D texture
    auto d3dHook = g_kanan->getD3D9Hook();
    if (!d3dHook) {
        return;
    }
    auto device = d3dHook->getDevice();
    if (!device) {
        return;
    }

    // Create texture with the same dimensions and A8R8G8B8 format
    IDirect3DTexture9* tex = nullptr;
    HRESULT hr = device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr);
    if (FAILED(hr) || tex == nullptr) {
        log("[CookingOverlay] CreateTexture failed (hr=%08x)", hr);
        return;
    }

    // Lock the texture and copy pixels, converting BGRA -> A8R8G8B8 (0xAARRGGBB)
    D3DLOCKED_RECT rect{};
    // D3DLOCK_DISCARD is invalid for MANAGED pool; use 0 flags here.
    if (FAILED(tex->LockRect(0, &rect, nullptr, 0))) {
        log("[CookingOverlay] LockRect failed");
        tex->Release();
        return;
    }

    // Source is BGRA (pixels vector). Destination expects A8R8G8B8 (0xAARRGGBB)
    auto dst = reinterpret_cast<uint8_t*>(rect.pBits);
    const int dstPitch = rect.Pitch;
    const uint8_t* src = pixels.data();
    const bool srcHad32 = (bm.bmBitsPixel == 32);
    const bool premultiplyAlpha = false; // set true if your pipeline expects premultiplied alpha

    for (int y = 0; y < height; ++y) {
        uint8_t* dstRow = dst + (y * dstPitch);
        const uint8_t* srcRow = src + (y * width * 4);
        for (int x = 0; x < width; ++x) {
            uint8_t b = srcRow[x * 4 + 0];
            uint8_t g = srcRow[x * 4 + 1];
            uint8_t r = srcRow[x * 4 + 2];
            uint8_t a = srcRow[x * 4 + 3];

            // Treat exact white as transparent (color-key)
            bool isWhite = (r == 255 && g == 255 && b == 255);

            if (isWhite) {
                a = 0x00; // fully transparent
            }
            else {
                a = 0xFF; // fully opaque
            }

            uint8_t outR = r;
            uint8_t outG = g;
            uint8_t outB = b;

            if (premultiplyAlpha && a != 0xFF) {
                float alphaF = (a / 255.0f);
                outR = static_cast<uint8_t>(outR * alphaF);
                outG = static_cast<uint8_t>(outG * alphaF);
                outB = static_cast<uint8_t>(outB * alphaF);
            }

            uint32_t pixel = (uint32_t(a) << 24) | (uint32_t(outR) << 16) | (uint32_t(outG) << 8) | uint32_t(outB);
            reinterpret_cast<uint32_t*>(dstRow)[x] = pixel;
        }
    }

    tex->UnlockRect(0);

    // Save member texture (release old if any)
    if (m_texture) {
        m_texture->Release();
        m_texture = nullptr;
    }

    m_texture = tex;
    m_w = width;
    m_h = height;

    log("[CookingOverlay] Created texture (%dx%d) from IDB_BITMAP1", m_w, m_h);
}

void CookingOverlay::releaseTexture() {
    if (m_texture) {
        m_texture->Release();
        m_texture = nullptr;
    }
    m_w = m_h = 0;
}

void CookingOverlay::onConfigLoad(const Config& cfg) {
    m_isEnabled = cfg.get<bool>("CookingOverlay.Enabled").value_or(false);
    m_scale = cfg.get<float>("CookingOverlay.Scale").value_or(1.0f);
    // clamp loaded value to safe range
    m_scale = std::clamp(m_scale, 0.05f, 10.0f);
}

void CookingOverlay::onConfigSave(Config& cfg) {
    cfg.set<bool>("CookingOverlay.Enabled", m_isEnabled);
    cfg.set<float>("CookingOverlay.Scale", m_scale);
}