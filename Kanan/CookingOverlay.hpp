#include "Mod.hpp"
#include <d3d9.h>
#include <imgui.h>

namespace kanan {
    class CookingOverlay : public Mod {
    public:
        CookingOverlay();

        std::string getName() override { return "Cooking Overlay"; }

        void onUI() override;
        bool onWindow() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

        virtual ~CookingOverlay();

    private:
        void drawWindow();
        bool ensureTexture();
        void createTextureFromResource();
        void releaseTexture();

        bool m_isEnabled;
        IDirect3DTexture9* m_texture;
        int m_w;
        int m_h;

        // Scale factor applied to both window size and displayed image (1.0 = 100%)
        float m_scale;

		// Replace pure black pixels with this color for user configurable color replacement. Default is opaque black (0, 0, 0, 1).
        ImVec4 m_replaceColor;
    };
}