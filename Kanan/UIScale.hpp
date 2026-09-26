#pragma once

#include <cstdint>
#include <memory>

#include <d3d9.h>

#include <FunctionHook.hpp>

#include "Mod.hpp"

namespace kanan {
    // Scales the whole 2D interface (windows, icons, text) like a lower UI resolution while the
    // 3D world keeps rendering at full resolution:
    //   - the interface is laid out for a smaller virtual screen (real size / scale),
    //   - the 2D projection draws that virtual screen over the real one,
    //   - mouse positions the interface reads are mapped into the virtual screen,
    //   - screen positions the client computes for the world (targeting, markers) are too.
    class UIScale : public Mod {
    public:
        UIScale();
        virtual ~UIScale();

        std::string getName() override { return "UI Scale"; }

        void onFrame() override;
        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

        // Scale currently applied to the interface (1 when off). Screen positions the game
        // computes for its interface are in virtual pixels; multiply by this for real pixels.
        static float current();

        // How the magnified interface is sampled.
        enum Filter : int {
            FILTER_SMOOTH,  // the client's own (bilinear) filtering: soft
            FILTER_SHARP,   // nearest texel: crisp, but uneven at fractional scales
            FILTER_CRISP,   // sharp bilinear: crisp with even strokes at any scale
            FILTER_PIXEL_ART, // xBR: diagonals and curves redrawn smooth at the output resolution
        };

    private:
        bool m_isEnabled;
        float m_scale;
        int m_filter;
        int m_curveSmoothing; // pixel art filter: 0 corners kept, diagonals only; 1 also slopes; 2 rounded

        float m_appliedScale;
        int m_realWidth;
        int m_realHeight;

        bool m_isReady;
        uintptr_t m_exlBegin;
        uintptr_t m_exlEnd;
        uintptr_t m_pleioneBegin;
        uintptr_t m_pleioneEnd;

        IDirect3DPixelShader9* m_filterShader;
        IDirect3DPixelShader9* m_xbrShader;

        std::unique_ptr<FunctionHook> m_resizeHook;
        std::unique_ptr<FunctionHook> m_screenToClientHook;
        std::unique_ptr<FunctionHook> m_backBufferExtentHook;
        std::unique_ptr<FunctionHook> m_screenFromWorldHook;
        std::unique_ptr<FunctionHook> m_screenFromProjectionHook;
        std::unique_ptr<FunctionHook> m_setTransformHook;
        std::unique_ptr<FunctionHook> m_drawPrimitiveHook;
        std::unique_ptr<FunctionHook> m_drawIndexedPrimitiveHook;
        std::unique_ptr<FunctionHook> m_setViewportHook;
        std::unique_ptr<FunctionHook> m_getViewportHook;
        std::unique_ptr<FunctionHook> m_setScissorRectHook;
        std::unique_ptr<FunctionHook> m_getScissorRectHook;

        float targetScale() const;
        void applyLayout(float scale);
        bool hookDevice(IDirect3DDevice9* device);
        HRESULT applyProjection(IDirect3DDevice9* device);
        bool isPleioneCaller(uintptr_t returnAddress) const;

        template <typename Draw>
        HRESULT drawInterface(IDirect3DDevice9* device, Draw draw);
        bool setShaderFilter(IDirect3DDevice9* device, IDirect3DPixelShader9* shader, float roundCorners, float smoothSlopes);

        static void __fastcall hookedResize(uintptr_t interfaceMgr, uintptr_t edx, uint32_t width, uint32_t height);
        static BOOL WINAPI hookedScreenToClient(HWND wnd, LPPOINT point);
        static void __fastcall hookedBackBufferExtent(uintptr_t context, uintptr_t edx, uint32_t* width, uint32_t* height);
        static void __fastcall hookedScreenFromWorld(uintptr_t context, uintptr_t edx, const void* position, int32_t* x, int32_t* y);
        static void __fastcall hookedScreenFromProjection(uintptr_t context, uintptr_t edx, float px, float py, int32_t* x, int32_t* y);
        static HRESULT WINAPI hookedSetTransform(IDirect3DDevice9* device, D3DTRANSFORMSTATETYPE state, CONST D3DMATRIX* matrix);
        static HRESULT WINAPI hookedDrawPrimitive(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, UINT start, UINT count);
        static HRESULT WINAPI hookedDrawIndexedPrimitive(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, INT base, UINT minIndex,
            UINT vertices, UINT startIndex, UINT count);
        static HRESULT WINAPI hookedSetViewport(IDirect3DDevice9* device, CONST D3DVIEWPORT9* viewport);
        static HRESULT WINAPI hookedGetViewport(IDirect3DDevice9* device, D3DVIEWPORT9* viewport);
        static HRESULT WINAPI hookedSetScissorRect(IDirect3DDevice9* device, CONST RECT* rect);
        static HRESULT WINAPI hookedGetScissorRect(IDirect3DDevice9* device, RECT* rect);
    };
}
