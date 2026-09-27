#pragma once

#include <Windows.h>

#include "Mod.hpp"

namespace kanan {
    // Fixes the client freezing for seconds at a time on CPUs with both performance and efficiency
    // cores (Intel 12th gen and later), where Windows can move the client's threads to the slower
    // efficiency cores and throttle them there. Does automatically what setting the client's
    // affinity in Task Manager does by hand every launch.
    class CpuScheduling : public Mod {
    public:
        CpuScheduling();

        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        enum Cores : int {
            ALL_CORES,
            PERFORMANCE_CORES,
            EFFICIENCY_CORES,
        };

        bool m_preventThrottling;
        int m_cores;

        // The client's own affinity, restored when all cores are chosen.
        DWORD_PTR m_originalAffinity;
        DWORD_PTR m_performanceAffinity;
        DWORD_PTR m_efficiencyAffinity;
        bool m_isHybrid;

        // Whether the client's scheduling was changed from what Windows gave it.
        bool m_isThrottlingChanged;
        bool m_isAffinityChanged;

        void detectCores();
        void apply();
        void applyThrottling();
        void applyAffinity();
    };
}
