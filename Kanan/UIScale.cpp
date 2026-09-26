#include <algorithm>
#include <cmath>
#include <intrin.h>

#include <Windows.h>
#include <Psapi.h>

#include <imgui.h>

#include <Scan.hpp>

#include "Kanan.hpp"
#include "Log.hpp"
#include "UIScale.hpp"
#include "UIScaleFilter.h"
#include "UIScaleXbr.h"

#pragma comment(lib, "psapi.lib")

using namespace std;

namespace kanan {
    static UIScale* g_uiScale{ nullptr };

    // Interface scale in effect this frame, read by the hooks and by other mods.
    static float g_appliedScale{ 1.0f };

    // True while the client draws its 2D interface (its pixel projection is the current one).
    static bool g_isDrawingInterface{ false };

    // The client's own (unscaled) pixel projection while it draws its interface.
    static D3DMATRIX g_projection{};

    // The last render size handed to the client, real and as the (virtual) size it was given.
    struct Extent {
        bool isScaled;
        int realWidth;
        int realHeight;
        int virtualWidth;
        int virtualHeight;
    };

    static Extent g_extent{};

    // Address range of this DLL. Kanan draws its own overlay through the same device, and those
    // calls are left alone.
    static uintptr_t g_selfBegin{ 0 };
    static uintptr_t g_selfEnd{ 0 };

    static bool isOwnCall(uintptr_t returnAddress) {
        return returnAddress >= g_selfBegin && returnAddress < g_selfEnd;
    }

    // IDirect3DDevice9 vtable indices.
    constexpr size_t setTransformIndex = 44;
    constexpr size_t drawPrimitiveIndex = 81;
    constexpr size_t drawIndexedPrimitiveIndex = 82;
    constexpr size_t setViewportIndex = 47;
    constexpr size_t getViewportIndex = 48;
    constexpr size_t setScissorRectIndex = 75;
    constexpr size_t getScissorRectIndex = 76;

    // Interface sub-areas (the minimap, portraits, clipped lists) are given to the device in
    // virtual pixels. They are widened to real pixels when set, and the client is handed back
    // what it set when it reads them (it saves and restores them around its drawing).
    struct ScaledViewport {
        bool isActive;
        D3DVIEWPORT9 requested;
        D3DVIEWPORT9 applied;
    };

    struct ScaledScissor {
        bool isActive;
        RECT requested;
        RECT applied;
    };

    static ScaledViewport g_viewport{};
    static ScaledScissor g_scissor{};

    static bool operator==(const D3DVIEWPORT9& a, const D3DVIEWPORT9& b) {
        return a.X == b.X && a.Y == b.Y && a.Width == b.Width && a.Height == b.Height;
    }

    static bool operator==(const RECT& a, const RECT& b) {
        return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
    }

    // Size of the surface being drawn to, when it is a screen-sized one (the back buffer or a
    // full-screen post-processing target) rather than a texture the client renders into.
    static bool getScreenTarget(IDirect3DDevice9* device, int realWidth, int realHeight, UINT& width, UINT& height) {
        IDirect3DSurface9* target{ nullptr };

        if (FAILED(device->GetRenderTarget(0, &target)) || target == nullptr) {
            return false;
        }

        D3DSURFACE_DESC desc{};
        auto isValid = SUCCEEDED(target->GetDesc(&desc));

        target->Release();

        if (!isValid || abs((int)desc.Width - realWidth) > 2 || abs((int)desc.Height - realHeight) > 2) {
            return false;
        }

        width = desc.Width;
        height = desc.Height;

        return true;
    }

    // Maps an area of the virtual screen onto the real one. Areas that already cover the whole
    // target or reach past the virtual screen are not the interface's and are left alone.
    static bool scaleArea(LONG& left, LONG& top, LONG& right, LONG& bottom, float scale, UINT width, UINT height) {
        auto virtualWidth = (LONG)ceilf(width / scale);
        auto virtualHeight = (LONG)ceilf(height / scale);
        auto isFullTarget = left <= 0 && top <= 0 && right >= (LONG)width - 2 && bottom >= (LONG)height - 2;

        if (isFullTarget || left < 0 || top < 0 || right > virtualWidth + 1 || bottom > virtualHeight + 1) {
            return false;
        }

        left = (LONG)floorf(left * scale);
        top = (LONG)floorf(top * scale);
        right = min((LONG)ceilf(right * scale), (LONG)width);
        bottom = min((LONG)ceilf(bottom * scale), (LONG)height);

        return true;
    }

    using GetInstance = uintptr_t(__cdecl*)();

    // The 2D projection the client sets before drawing its interface: an orthographic mapping of
    // pixels onto the screen (x' = 2x/w - 1, y' = 1 - 2y/h), with identity view and world.
    static bool isPixelProjection(const D3DMATRIX& m) {
        auto zero = [](float v) { return fabsf(v) < 1e-6f; };
        auto one = [](float v) { return fabsf(v - 1.0f) < 1e-6f; };

        return m._11 > 0.0f && m._22 < 0.0f &&
            zero(m._12) && zero(m._13) && zero(m._14) &&
            zero(m._21) && zero(m._23) && zero(m._24) &&
            zero(m._31) && zero(m._32) && one(m._33) && zero(m._34) &&
            one(-m._41) && one(m._42) && zero(m._43) && one(m._44);
    }

    // Address range of a loaded module.
    static bool getModuleRange(const char* name, uintptr_t& begin, uintptr_t& end) {
        auto module = GetModuleHandleA(name);
        MODULEINFO info{};

        if (module == nullptr || !GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info))) {
            return false;
        }

        begin = (uintptr_t)info.lpBaseOfDll;
        end = begin + info.SizeOfImage;

        return true;
    }

    UIScale::UIScale()
        : m_isEnabled{ false },
        m_scale{ 1.5f },
        m_filter{ FILTER_CRISP },
        m_curveSmoothing{ 0 },
        m_appliedScale{ 1.0f },
        m_realWidth{ 0 },
        m_realHeight{ 0 },
        m_isReady{ false },
        m_exlBegin{ 0 },
        m_exlEnd{ 0 },
        m_pleioneBegin{ 0 },
        m_pleioneEnd{ 0 },
        m_filterShader{ nullptr },
        m_xbrShader{ nullptr }
    {
        log("[UIScale] Entering constructor...");

        g_uiScale = this;

        HMODULE self{};
        MODULEINFO selfInfo{};

        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&isOwnCall, &self) &&
            GetModuleInformation(GetCurrentProcess(), self, &selfInfo, sizeof(selfInfo)))
        {
            g_selfBegin = (uintptr_t)selfInfo.lpBaseOfDll;
            g_selfEnd = g_selfBegin + selfInfo.SizeOfImage;
        }

        // Mouse input: EXL.dll's console reads the cursor with GetCursorPos + ScreenToClient
        // and feeds that position to the interface. Pleione.dll is the client itself.
        if (!getModuleRange("EXL.dll", m_exlBegin, m_exlEnd) || !getModuleRange("Pleione.dll", m_pleioneBegin, m_pleioneEnd)) {
            log("[UIScale] EXL.dll or Pleione.dll is not loaded");
            log("[UIScale] Leaving constructor");
            return;
        }

        // pleione::CInterfaceMgr's resize handler: stores the interface screen size in
        // CWindowMgr (+0x4EC/+0x4EE) and moves anchored windows by the change.
        auto resize = scan("Pleione.dll", "55 8B EC 51 51 53 56 57 8D 45 F8 50 8D 45 FC 8B F1 8B 0D ? ? ? ? 50 E8 ? ? ? ? 8B 7D 0C 8B 5D 08");
        auto screenToClient = GetProcAddress(GetModuleHandleA("user32.dll"), "ScreenToClient");

        if (!resize || screenToClient == nullptr) {
            log("[UIScale] Failed to find the interface resize handler");
            log("[UIScale] Leaving constructor");
            return;
        }

        // The client turns world positions into screen pixels (and back) with the render size it
        // gets from the renderer, or with the renderer's own conversions. It compares those pixels
        // with the mouse (picking, Ctrl targeting) and draws markers with them, so the client is
        // given the virtual size and virtual positions.
        auto renderer = GetModuleHandleA("Renderer2.dll");
        auto backBufferExtent = GetProcAddress(renderer, "?GetBackBufferExtent@CRendererContext@pleione@@UAEXAAK0@Z");
        auto screenFromWorld = GetProcAddress(renderer,
            "?GetScreenSpacePositionFromWorldSpacePosition@CRendererContext@pleione@@UAEXABV?$_vector@U?$__vector4@M@esl@@@esl@@AAJ1@Z");
        auto screenFromProjection = GetProcAddress(renderer, "?GetScreenSpacePositionFromProjectionPosition@CRendererContext@pleione@@UAEXMMAAJ0@Z");

        if (backBufferExtent == nullptr || screenFromWorld == nullptr || screenFromProjection == nullptr) {
            log("[UIScale] Failed to find the renderer's screen conversions");
            log("[UIScale] Leaving constructor");
            return;
        }

        m_resizeHook = make_unique<FunctionHook>(*resize, (uintptr_t)&UIScale::hookedResize);
        m_screenToClientHook = make_unique<FunctionHook>((uintptr_t)screenToClient, (uintptr_t)&UIScale::hookedScreenToClient);
        m_backBufferExtentHook = make_unique<FunctionHook>((uintptr_t)backBufferExtent, (uintptr_t)&UIScale::hookedBackBufferExtent);
        m_screenFromWorldHook = make_unique<FunctionHook>((uintptr_t)screenFromWorld, (uintptr_t)&UIScale::hookedScreenFromWorld);
        m_screenFromProjectionHook = make_unique<FunctionHook>((uintptr_t)screenFromProjection, (uintptr_t)&UIScale::hookedScreenFromProjection);

        m_isReady = m_resizeHook->isValid() && m_screenToClientHook->isValid() && m_backBufferExtentHook->isValid() &&
            m_screenFromWorldHook->isValid() && m_screenFromProjectionHook->isValid();

        log("[UIScale] %s", m_isReady ? "Hooked interface resize and mouse" : "Failed to hook interface resize or mouse");
        log("[UIScale] Leaving constructor");
    }

    UIScale::~UIScale() {
        for (auto hook : { &m_resizeHook, &m_screenToClientHook, &m_backBufferExtentHook, &m_screenFromWorldHook, &m_screenFromProjectionHook,
            &m_setTransformHook, &m_drawPrimitiveHook, &m_drawIndexedPrimitiveHook, &m_setViewportHook, &m_getViewportHook,
            &m_setScissorRectHook, &m_getScissorRectHook })
        {
            if (*hook) {
                (*hook)->remove();
            }
        }

        for (auto shader : { m_filterShader, m_xbrShader }) {
            if (shader != nullptr) {
                shader->Release();
            }
        }

        g_appliedScale = 1.0f;
        g_uiScale = nullptr;
    }

    float UIScale::current() {
        return g_appliedScale;
    }

    float UIScale::targetScale() const {
        return m_isEnabled ? m_scale : 1.0f;
    }

    bool UIScale::isPleioneCaller(uintptr_t returnAddress) const {
        return returnAddress >= m_pleioneBegin && returnAddress < m_pleioneEnd;
    }

    // Lays the interface out again for a virtual screen of real size / scale.
    void UIScale::applyLayout(float scale) {
        auto pleione = GetModuleHandleA("Pleione.dll");
        auto getInstance = (GetInstance)GetProcAddress(pleione, "?GetInstance@?$TSingleton@VCInterfaceMgr@pleione@@@esl@@SAAAVCInterfaceMgr@pleione@@XZ");

        if (getInstance == nullptr || m_realWidth <= 0 || m_realHeight <= 0) {
            return;
        }

        auto resize = (decltype(hookedResize)*)m_resizeHook->getOriginal();

        g_appliedScale = scale;
        m_appliedScale = scale;
        resize(getInstance(), 0, (uint32_t)lround(m_realWidth / scale), (uint32_t)lround(m_realHeight / scale));

        log("[UIScale] Interface laid out at %dx%d (scale %.2f)", (int)lround(m_realWidth / scale), (int)lround(m_realHeight / scale), scale);
    }

    void UIScale::onFrame() {
        if (!m_isReady) {
            return;
        }

        // The 2D projection is set through the device, which only exists once rendering has begun.
        if (m_setTransformHook == nullptr) {
            auto d3d9 = g_kanan->getD3D9Hook();

            if (d3d9 == nullptr || d3d9->getDevice() == nullptr) {
                return;
            }

            if (!hookDevice(d3d9->getDevice())) {
                log("[UIScale] Failed to hook the Direct3D device");
                m_isReady = false;
                return;
            }
        }

        // Present ends the client's frame; Kanan's own overlay is drawn after this.
        g_isDrawingInterface = false;

        // The real size normally comes from the client's own resize calls; fall back to the window.
        if (m_realWidth <= 0 || m_realHeight <= 0) {
            RECT client{};

            if (GetClientRect(g_kanan->getWindow(), &client)) {
                m_realWidth = client.right - client.left;
                m_realHeight = client.bottom - client.top;
            }
        }

        if (targetScale() != m_appliedScale) {
            applyLayout(targetScale());
        }
    }

    bool UIScale::hookDevice(IDirect3DDevice9* device) {
        auto vtable = *(void***)device;

        m_setTransformHook = make_unique<FunctionHook>((uintptr_t)vtable[setTransformIndex], (uintptr_t)&UIScale::hookedSetTransform);
        m_drawPrimitiveHook = make_unique<FunctionHook>((uintptr_t)vtable[drawPrimitiveIndex], (uintptr_t)&UIScale::hookedDrawPrimitive);
        m_drawIndexedPrimitiveHook = make_unique<FunctionHook>((uintptr_t)vtable[drawIndexedPrimitiveIndex], (uintptr_t)&UIScale::hookedDrawIndexedPrimitive);
        m_setViewportHook = make_unique<FunctionHook>((uintptr_t)vtable[setViewportIndex], (uintptr_t)&UIScale::hookedSetViewport);
        m_getViewportHook = make_unique<FunctionHook>((uintptr_t)vtable[getViewportIndex], (uintptr_t)&UIScale::hookedGetViewport);
        m_setScissorRectHook = make_unique<FunctionHook>((uintptr_t)vtable[setScissorRectIndex], (uintptr_t)&UIScale::hookedSetScissorRect);
        m_getScissorRectHook = make_unique<FunctionHook>((uintptr_t)vtable[getScissorRectIndex], (uintptr_t)&UIScale::hookedGetScissorRect);

        // Crisp filtering needs a pixel shader; without one it falls back to sharp pixels.
        if (FAILED(device->CreatePixelShader((const DWORD*)g_uiScaleFilter, &m_filterShader))) {
            log("[UIScale] Failed to create the crisp filter shader");
            m_filterShader = nullptr;
        }

        // Needs pixel shader 2.a (dependent reads, 222 instructions); without it the pixel art
        // filter falls back to crisp.
        if (FAILED(device->CreatePixelShader((const DWORD*)g_uiScaleXbr, &m_xbrShader))) {
            log("[UIScale] Failed to create the pixel art filter shader");
            m_xbrShader = nullptr;
        }

        return m_setTransformHook->isValid() && m_drawPrimitiveHook->isValid() && m_drawIndexedPrimitiveHook->isValid() &&
            m_setViewportHook->isValid() && m_getViewportHook->isValid() && m_setScissorRectHook->isValid() && m_getScissorRectHook->isValid();
    }

    void UIScale::onUI() {
        if (!m_isReady) {
            return;
        }

        if (ImGui::TreeNode("UI Scale")) {
            ImGui::TextWrapped("Scales the whole interface (windows, icons and text) while the game world stays at full resolution.");
            ImGui::Spacing();
            ImGui::Checkbox("Enabled##UIScale", &m_isEnabled);
            ImGui::SliderFloat("Scale##UIScale", &m_scale, 1.0f, 3.0f, "%.2fx");
            ImGui::Combo("Filter##UIScale", &m_filter, "Smooth\0Sharp pixels\0Crisp\0Pixel art (xBR)\0");

            if (m_filter == FILTER_PIXEL_ART) {
                ImGui::Combo("Curve smoothing##UIScale", &m_curveSmoothing, "Low (keeps corners)\0Medium\0High (rounded)\0");
            }
            ImGui::TextDisabled("Pixel art redraws curves and diagonals of text and icons smoothly at any scale.\n"
                "Crisp keeps them sharp with even strokes. Sharp pixels is blockier at in-between\n"
                "scales like 1.5x. Smooth is the game's own blur.");
            ImGui::TreePop();
        }
    }

    void UIScale::onConfigLoad(const Config& cfg) {
        m_isEnabled = cfg.get<bool>("UIScale.Enabled").value_or(false);
        m_scale = cfg.get<float>("UIScale.Scale").value_or(1.5f);
        m_filter = cfg.get<int>("UIScale.Filter").value_or(FILTER_CRISP);
        m_curveSmoothing = std::clamp(cfg.get<int>("UIScale.CurveSmoothing").value_or(0), 0, 2);

        if (m_filter < FILTER_SMOOTH || m_filter > FILTER_PIXEL_ART) {
            m_filter = FILTER_CRISP;
        }
    }

    void UIScale::onConfigSave(Config& cfg) {
        cfg.set<bool>("UIScale.Enabled", m_isEnabled);
        cfg.set<float>("UIScale.Scale", m_scale);
        cfg.set<int>("UIScale.Filter", m_filter);
        cfg.set<int>("UIScale.CurveSmoothing", m_curveSmoothing);
    }

    // The client reports its size; lay the interface out for the virtual size instead. The size
    // usually comes from the render size, which the client is already given as virtual, so
    // that is mapped back to the real size first.
    void UIScale::hookedResize(uintptr_t interfaceMgr, uintptr_t edx, uint32_t width, uint32_t height) {
        auto self = g_uiScale;
        auto orig = (decltype(hookedResize)*)self->m_resizeHook->getOriginal();
        auto scale = self->targetScale();
        int realWidth = (int16_t)width;
        int realHeight = (int16_t)height;

        if (g_extent.isScaled && realWidth == g_extent.virtualWidth && realHeight == g_extent.virtualHeight) {
            realWidth = g_extent.realWidth;
            realHeight = g_extent.realHeight;
        }

        self->m_realWidth = realWidth;
        self->m_realHeight = realHeight;
        self->m_appliedScale = scale;
        g_appliedScale = scale;

        orig(interfaceMgr, edx, (uint32_t)lround(self->m_realWidth / scale), (uint32_t)lround(self->m_realHeight / scale));
    }

    // Mouse positions EXL.dll hands to the interface are mapped into the virtual screen.
    BOOL UIScale::hookedScreenToClient(HWND wnd, LPPOINT point) {
        auto self = g_uiScale;
        auto orig = (decltype(hookedScreenToClient)*)self->m_screenToClientHook->getOriginal();
        auto caller = (uintptr_t)_ReturnAddress();
        auto result = orig(wnd, point);
        auto scale = g_appliedScale;

        if (result && point != nullptr && scale != 1.0f && caller >= self->m_exlBegin && caller < self->m_exlEnd) {
            point->x = (LONG)floorf(point->x / scale);
            point->y = (LONG)floorf(point->y / scale);
        }

        return result;
    }

    // The render size as the client sees it: the virtual screen. Its world picking (pick::Reset
    // divides the virtual mouse position by this), Ctrl targeting and screen markers all project
    // with it. The renderer's own callers keep the real size.
    void UIScale::hookedBackBufferExtent(uintptr_t context, uintptr_t edx, uint32_t* width, uint32_t* height) {
        auto self = g_uiScale;
        auto orig = (decltype(hookedBackBufferExtent)*)self->m_backBufferExtentHook->getOriginal();
        auto caller = (uintptr_t)_ReturnAddress();
        auto scale = g_appliedScale;

        orig(context, edx, width, height);

        if (scale != 1.0f && width != nullptr && height != nullptr && self->isPleioneCaller(caller)) {
            g_extent.realWidth = (int)*width;
            g_extent.realHeight = (int)*height;

            *width = (uint32_t)lround(*width / scale);
            *height = (uint32_t)lround(*height / scale);

            g_extent.virtualWidth = (int)*width;
            g_extent.virtualHeight = (int)*height;
            g_extent.isScaled = true;
        }
    }

    // World (and projected) positions the renderer converts to screen pixels for the client.
    void UIScale::hookedScreenFromWorld(uintptr_t context, uintptr_t edx, const void* position, int32_t* x, int32_t* y) {
        auto self = g_uiScale;
        auto orig = (decltype(hookedScreenFromWorld)*)self->m_screenFromWorldHook->getOriginal();
        auto caller = (uintptr_t)_ReturnAddress();
        auto scale = g_appliedScale;

        orig(context, edx, position, x, y);

        if (scale != 1.0f && x != nullptr && y != nullptr && self->isPleioneCaller(caller)) {
            *x = (int32_t)floorf(*x / scale);
            *y = (int32_t)floorf(*y / scale);
        }
    }

    void UIScale::hookedScreenFromProjection(uintptr_t context, uintptr_t edx, float px, float py, int32_t* x, int32_t* y) {
        auto self = g_uiScale;
        auto orig = (decltype(hookedScreenFromProjection)*)self->m_screenFromProjectionHook->getOriginal();
        auto caller = (uintptr_t)_ReturnAddress();
        auto scale = g_appliedScale;

        orig(context, edx, px, py, x, y);

        if (scale != 1.0f && x != nullptr && y != nullptr && self->isPleioneCaller(caller)) {
            *x = (int32_t)floorf(*x / scale);
            *y = (int32_t)floorf(*y / scale);
        }
    }

    // Draw the virtual screen over the real one: the client's 2D projection maps pixels of its
    // (virtual) interface layout; widen each pixel by the scale.
    HRESULT UIScale::hookedSetTransform(IDirect3DDevice9* device, D3DTRANSFORMSTATETYPE state, CONST D3DMATRIX* matrix) {
        auto self = g_uiScale;
        auto orig = (decltype(hookedSetTransform)*)self->m_setTransformHook->getOriginal();

        if (isOwnCall((uintptr_t)_ReturnAddress())) {
            return orig(device, state, matrix);
        }

        if (state == D3DTS_PROJECTION) {
            g_isDrawingInterface = matrix != nullptr && isPixelProjection(*matrix);

            if (g_isDrawingInterface) {
                g_projection = *matrix;

                return self->applyProjection(device);
            }
        }

        return orig(device, state, matrix);
    }

    // Sets the client's pixel projection widened by the scale. It depends on the viewport, so it
    // is set again whenever the viewport changes while the interface is being drawn.
    HRESULT UIScale::applyProjection(IDirect3DDevice9* device) {
        auto orig = (decltype(hookedSetTransform)*)m_setTransformHook->getOriginal();
        auto scale = g_appliedScale;

        if (scale != 1.0f) {
            // The real viewport, so a scaled interface sub-area keeps the client's own pixel units.
            auto getViewport = (decltype(hookedGetViewport)*)m_getViewportHook->getOriginal();
            D3DVIEWPORT9 viewport{};

            if (SUCCEEDED(getViewport(device, &viewport)) && viewport.Width > 0 && viewport.Height > 0) {
                auto scaled = g_projection;

                scaled._11 = 2.0f * scale / viewport.Width;
                scaled._22 = -2.0f * scale / viewport.Height;

                return orig(device, D3DTS_PROJECTION, &scaled);
            }
        }

        return orig(device, D3DTS_PROJECTION, &g_projection);
    }

    // What texture stage 0 does with one operand: take the texture, or the diffuse color (which
    // is also what "current" means on the first stage). Anything else is left to the client.
    enum class Operand { TEXTURE, DIFFUSE, OTHER };

    static Operand getOperand(DWORD arg) {
        switch (arg) {
        case D3DTA_TEXTURE: return Operand::TEXTURE;
        case D3DTA_DIFFUSE:
        case D3DTA_CURRENT: return Operand::DIFFUSE;
        default: return Operand::OTHER;
        }
    }

    // Whether the texture and the diffuse color are used by one operation of stage 0.
    static bool getInputs(IDirect3DDevice9* device, D3DTEXTURESTAGESTATETYPE opState, D3DTEXTURESTAGESTATETYPE arg1State,
        D3DTEXTURESTAGESTATETYPE arg2State, float& useTexture, float& useDiffuse)
    {
        DWORD op{}, arg1{}, arg2{};

        device->GetTextureStageState(0, opState, &op);
        device->GetTextureStageState(0, arg1State, &arg1);
        device->GetTextureStageState(0, arg2State, &arg2);

        auto a = getOperand(arg1);
        auto b = getOperand(arg2);

        switch (op) {
        case D3DTOP_MODULATE:
            if (a == Operand::OTHER || b == Operand::OTHER || a == b) {
                return false;
            }

            useTexture = 1.0f;
            useDiffuse = 1.0f;
            return true;

        case D3DTOP_SELECTARG1:
        case D3DTOP_SELECTARG2: {
            auto selected = op == D3DTOP_SELECTARG1 ? a : b;

            if (selected == Operand::OTHER) {
                return false;
            }

            useTexture = selected == Operand::TEXTURE ? 1.0f : 0.0f;
            useDiffuse = selected == Operand::DIFFUSE ? 1.0f : 0.0f;
            return true;
        }

        default:
            return false;
        }
    }

    // Replaces stage 0 with the sharp bilinear shader when the client draws a plain textured,
    // colored quad (nearly all of the interface). Returns false to leave the draw to the client.
    bool UIScale::setShaderFilter(IDirect3DDevice9* device, IDirect3DPixelShader9* shader, float roundCorners, float smoothSlopes) {
        IDirect3DPixelShader9* current{ nullptr };

        if (shader == nullptr || FAILED(device->GetPixelShader(&current))) {
            return false;
        }

        if (current != nullptr) {
            current->Release();
            return false;
        }

        DWORD nextOp{}, texCoordIndex{}, transform{};

        device->GetTextureStageState(1, D3DTSS_COLOROP, &nextOp);
        device->GetTextureStageState(0, D3DTSS_TEXCOORDINDEX, &texCoordIndex);
        device->GetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, &transform);

        if (nextOp != D3DTOP_DISABLE || texCoordIndex != 0 || transform != D3DTTFF_DISABLE) {
            return false;
        }

        float colorTexture{}, colorDiffuse{}, alphaTexture{}, alphaDiffuse{};

        if (!getInputs(device, D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_COLORARG2, colorTexture, colorDiffuse) ||
            !getInputs(device, D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1, D3DTSS_ALPHAARG2, alphaTexture, alphaDiffuse))
        {
            return false;
        }

        IDirect3DBaseTexture9* texture{ nullptr };

        if (FAILED(device->GetTexture(0, &texture)) || texture == nullptr) {
            return false;
        }

        D3DSURFACE_DESC desc{};
        auto isValid = texture->GetType() == D3DRTYPE_TEXTURE && SUCCEEDED(((IDirect3DTexture9*)texture)->GetLevelDesc(0, &desc));

        texture->Release();

        if (!isValid || desc.Width == 0 || desc.Height == 0) {
            return false;
        }

        const float constants[4][4]{
            { (float)desc.Width, (float)desc.Height, 1.0f / desc.Width, 1.0f / desc.Height },
            { g_appliedScale, roundCorners, smoothSlopes, 0.0f },
            { colorTexture, colorTexture, colorTexture, alphaTexture },
            { colorDiffuse, colorDiffuse, colorDiffuse, alphaDiffuse },
        };

        device->SetPixelShaderConstantF(0, &constants[0][0], 4);
        device->SetPixelShader(shader);

        return true;
    }

    // Draws part of the client's interface with the chosen filter. The client's filters and
    // shader are restored afterwards so its own state cache (and the 3D world) are unaffected.
    template <typename Draw>
    HRESULT UIScale::drawInterface(IDirect3DDevice9* device, Draw draw) {
        if (!g_isDrawingInterface || g_appliedScale == 1.0f || m_filter == FILTER_SMOOTH) {
            return draw();
        }

        // The pixel art filter reads exact texels; crisp blends neighbors itself. Either falls
        // back to the next simpler filter when the draw can't use a shader.
        auto isPixelArt = m_filter == FILTER_PIXEL_ART &&
            setShaderFilter(device, m_xbrShader, m_curveSmoothing >= 2 ? 1.0f : 0.0f, m_curveSmoothing >= 1 ? 1.0f : 0.0f);
        auto isCrisp = !isPixelArt && m_filter >= FILTER_CRISP && setShaderFilter(device, m_filterShader, 0.0f, 0.0f);
        auto filter = isCrisp ? D3DTEXF_LINEAR : D3DTEXF_POINT;
        DWORD mag{}, min{};

        device->GetSamplerState(0, D3DSAMP_MAGFILTER, &mag);
        device->GetSamplerState(0, D3DSAMP_MINFILTER, &min);
        device->SetSamplerState(0, D3DSAMP_MAGFILTER, filter);
        device->SetSamplerState(0, D3DSAMP_MINFILTER, filter);

        auto result = draw();

        device->SetSamplerState(0, D3DSAMP_MAGFILTER, mag);
        device->SetSamplerState(0, D3DSAMP_MINFILTER, min);

        if (isCrisp || isPixelArt) {
            device->SetPixelShader(nullptr);
        }

        return result;
    }

    HRESULT UIScale::hookedDrawPrimitive(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, UINT start, UINT count) {
        auto orig = (decltype(hookedDrawPrimitive)*)g_uiScale->m_drawPrimitiveHook->getOriginal();

        if (isOwnCall((uintptr_t)_ReturnAddress())) {
            return orig(device, type, start, count);
        }

        return g_uiScale->drawInterface(device, [&] { return orig(device, type, start, count); });
    }

    HRESULT UIScale::hookedDrawIndexedPrimitive(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, INT base, UINT minIndex,
        UINT vertices, UINT startIndex, UINT count)
    {
        auto orig = (decltype(hookedDrawIndexedPrimitive)*)g_uiScale->m_drawIndexedPrimitiveHook->getOriginal();

        if (isOwnCall((uintptr_t)_ReturnAddress())) {
            return orig(device, type, base, minIndex, vertices, startIndex, count);
        }

        return g_uiScale->drawInterface(device, [&] { return orig(device, type, base, minIndex, vertices, startIndex, count); });
    }

    HRESULT UIScale::hookedSetViewport(IDirect3DDevice9* device, CONST D3DVIEWPORT9* viewport) {
        auto self = g_uiScale;
        auto orig = (decltype(hookedSetViewport)*)self->m_setViewportHook->getOriginal();
        auto scale = g_appliedScale;

        if (isOwnCall((uintptr_t)_ReturnAddress())) {
            return orig(device, viewport);
        }

        UINT width{}, height{};

        HRESULT result{};

        g_viewport.isActive = false;

        LONG left{}, top{}, right{}, bottom{};

        if (viewport != nullptr) {
            left = viewport->X;
            top = viewport->Y;
            right = left + viewport->Width;
            bottom = top + viewport->Height;
        }

        if (viewport != nullptr && scale != 1.0f && getScreenTarget(device, self->m_realWidth, self->m_realHeight, width, height) &&
            scaleArea(left, top, right, bottom, scale, width, height) && right > left && bottom > top)
        {
            auto scaled = *viewport;

            scaled.X = (DWORD)left;
            scaled.Y = (DWORD)top;
            scaled.Width = (DWORD)(right - left);
            scaled.Height = (DWORD)(bottom - top);

            result = orig(device, &scaled);

            if (SUCCEEDED(result)) {
                g_viewport = { true, *viewport, scaled };
            }
        }
        else {
            result = orig(device, viewport);
        }

        if (SUCCEEDED(result) && g_isDrawingInterface) {
            self->applyProjection(device);
        }

        return result;
    }

    HRESULT UIScale::hookedGetViewport(IDirect3DDevice9* device, D3DVIEWPORT9* viewport) {
        auto orig = (decltype(hookedGetViewport)*)g_uiScale->m_getViewportHook->getOriginal();
        auto result = orig(device, viewport);

        if (!isOwnCall((uintptr_t)_ReturnAddress()) && SUCCEEDED(result) && viewport != nullptr && g_viewport.isActive && *viewport == g_viewport.applied) {
            *viewport = g_viewport.requested;
        }

        return result;
    }

    HRESULT UIScale::hookedSetScissorRect(IDirect3DDevice9* device, CONST RECT* rect) {
        auto self = g_uiScale;
        auto orig = (decltype(hookedSetScissorRect)*)self->m_setScissorRectHook->getOriginal();
        auto scale = g_appliedScale;

        if (isOwnCall((uintptr_t)_ReturnAddress())) {
            return orig(device, rect);
        }

        UINT width{}, height{};

        g_scissor.isActive = false;

        if (rect == nullptr || scale == 1.0f || !getScreenTarget(device, self->m_realWidth, self->m_realHeight, width, height)) {
            return orig(device, rect);
        }

        auto scaled = *rect;

        if (!scaleArea(scaled.left, scaled.top, scaled.right, scaled.bottom, scale, width, height)) {
            return orig(device, rect);
        }

        auto result = orig(device, &scaled);

        if (SUCCEEDED(result)) {
            g_scissor = { true, *rect, scaled };
        }

        return result;
    }

    HRESULT UIScale::hookedGetScissorRect(IDirect3DDevice9* device, RECT* rect) {
        auto orig = (decltype(hookedGetScissorRect)*)g_uiScale->m_getScissorRectHook->getOriginal();
        auto result = orig(device, rect);

        if (!isOwnCall((uintptr_t)_ReturnAddress()) && SUCCEEDED(result) && rect != nullptr && g_scissor.isActive && *rect == g_scissor.applied) {
            *rect = g_scissor.requested;
        }

        return result;
    }
}
