#pragma once

#include "Mod.hpp"

namespace kanan {
    class FieldOfView : public Mod {
    public:
        FieldOfView();

        std::string getName() override { return "Field Of View"; }

        void onFrame() override;

        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        float m_fov;
        bool m_isEnabled;
    };
}
