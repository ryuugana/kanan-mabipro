#pragma once

#include <atomic>
#include <string>

#include <d3d9.h>

#include "Mod.hpp"

namespace kanan {
    // Saves a lossless PNG copy of each screenshot next to the game's JPEG (which is kept, as the
    // game can read it back). The PNG is written in the background, so taking a screenshot doesn't
    // stall the game any longer than it already does.
    class LosslessScreenshots : public Mod {
    public:
        LosslessScreenshots();

        void onFrame() override;

        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        bool m_isEnabled;

        // Results of the PNGs written in the background, logged from the game's thread.
        std::atomic<unsigned int> m_saved;
        std::atomic<unsigned int> m_failed;

        void savePng(const std::wstring& path, IDirect3DSurface9* surface, const RECT* rect);
    };
}
