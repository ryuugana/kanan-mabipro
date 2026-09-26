#include <bitset>
#include <cstdint>
#include <vector>

#include <imgui.h>

#include "Log.hpp"
#include "CpuScheduling.hpp"

using namespace std;

namespace kanan {
    // Windows 11 also ignores the timer resolution a throttled process asks for.
#ifndef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
    constexpr ULONG PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION = 0x4;
#endif

    // Looked up at runtime, as older versions of Windows don't have them.
    using GetSystemCpuSetInformationFn = BOOL(WINAPI*)(PSYSTEM_CPU_SET_INFORMATION information, ULONG length,
        PULONG returnedLength, HANDLE process, ULONG flags);
    using SetProcessInformationFn = BOOL(WINAPI*)(HANDLE process, PROCESS_INFORMATION_CLASS informationClass,
        LPVOID information, DWORD size);

    static size_t countProcessors(DWORD_PTR affinity) {
        return bitset<sizeof(DWORD_PTR) * 8>{ affinity }.count();
    }

    CpuScheduling::CpuScheduling()
        : m_preventThrottling{ false },
        m_cores{ ALL_CORES },
        m_originalAffinity{ 0 },
        m_performanceAffinity{ 0 },
        m_efficiencyAffinity{ 0 },
        m_isHybrid{ false },
        m_isThrottlingChanged{ false },
        m_isAffinityChanged{ false }
    {
        log("[CpuScheduling] Entering constructor");

        detectCores();

        log("[CpuScheduling] Leaving constructor");
    }

    void CpuScheduling::detectCores() {
        DWORD_PTR systemAffinity{};

        if (!GetProcessAffinityMask(GetCurrentProcess(), &m_originalAffinity, &systemAffinity)) {
            log("[CpuScheduling] Failed to get the client's affinity");
            return;
        }

        auto getCpuSets = (GetSystemCpuSetInformationFn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
            "GetSystemCpuSetInformation");

        if (getCpuSets == nullptr) {
            log("[CpuScheduling] This version of Windows doesn't report core types");
            return;
        }

        ULONG size{};

        getCpuSets(nullptr, 0, &size, GetCurrentProcess(), 0);

        vector<uint8_t> buffer(size);

        if (size == 0 || !getCpuSets((PSYSTEM_CPU_SET_INFORMATION)buffer.data(), size, &size, GetCurrentProcess(), 0)) {
            log("[CpuScheduling] Failed to get the CPU's core types");
            return;
        }

        // Each logical processor's efficiency class: higher is faster. CPUs with one kind of core
        // report the same class for all of them.
        vector<pair<BYTE, DWORD_PTR>> processors{};
        BYTE highestClass{ 0 };

        for (ULONG offset = 0; offset < size; ) {
            auto info = (PSYSTEM_CPU_SET_INFORMATION)&buffer[offset];

            if (info->Size == 0) {
                break;
            }

            offset += info->Size;

            // The client is 32-bit, so its affinity only covers the first 32 logical processors.
            if (info->Type != CpuSetInformation || info->CpuSet.Group != 0 ||
                info->CpuSet.LogicalProcessorIndex >= sizeof(DWORD_PTR) * 8)
            {
                continue;
            }

            auto processor = (DWORD_PTR)1 << info->CpuSet.LogicalProcessorIndex;

            if ((systemAffinity & processor) == 0) {
                continue;
            }

            processors.emplace_back(info->CpuSet.EfficiencyClass, processor);
            highestClass = max(highestClass, info->CpuSet.EfficiencyClass);
        }

        // Every slower class counts as efficiency cores (some CPUs have two kinds of them).
        for (auto& [efficiencyClass, processor] : processors) {
            if (efficiencyClass == highestClass) {
                m_performanceAffinity |= processor;
            }
            else {
                m_efficiencyAffinity |= processor;
            }
        }

        m_isHybrid = m_performanceAffinity != 0 && m_efficiencyAffinity != 0;

        if (m_isHybrid) {
            log("[CpuScheduling] %u performance and %u efficiency logical processors (%08X, %08X)",
                countProcessors(m_performanceAffinity), countProcessors(m_efficiencyAffinity),
                m_performanceAffinity, m_efficiencyAffinity);
        }
        else {
            log("[CpuScheduling] %u logical processors, all of one kind", countProcessors(m_performanceAffinity));
        }
    }

    void CpuScheduling::apply() {
        applyThrottling();
        applyAffinity();
    }

    void CpuScheduling::applyThrottling() {
        // Nothing to undo.
        if (!m_preventThrottling && !m_isThrottlingChanged) {
            return;
        }

        auto setProcessInformation = (SetProcessInformationFn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
            "SetProcessInformation");

        if (setProcessInformation == nullptr) {
            log("[CpuScheduling] This version of Windows doesn't throttle processes");
            return;
        }

        // Opting out of throttling is controlling it with it turned off; controlling nothing hands
        // it back to Windows.
        PROCESS_POWER_THROTTLING_STATE state{};

        state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        state.ControlMask = m_preventThrottling ?
            PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION : 0;
        state.StateMask = 0;

        auto result = setProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));

        // Windows 10 doesn't know about timer resolution throttling.
        if (!result && m_preventThrottling) {
            state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
            result = setProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
        }

        if (result) {
            m_isThrottlingChanged = m_preventThrottling;
            log("[CpuScheduling] Power throttling %s", m_preventThrottling ? "prevented" : "restored");
        }
        else {
            log("[CpuScheduling] Failed to change power throttling (%u)", GetLastError());
        }
    }

    void CpuScheduling::applyAffinity() {
        if (!m_isHybrid) {
            return;
        }

        DWORD_PTR affinity{};

        switch (m_cores) {
        case PERFORMANCE_CORES:
            affinity = m_performanceAffinity;
            break;

        case EFFICIENCY_CORES:
            affinity = m_efficiencyAffinity;
            break;

        default:
            // Nothing to undo.
            if (!m_isAffinityChanged) {
                return;
            }

            affinity = m_originalAffinity;
            break;
        }

        if (SetProcessAffinityMask(GetCurrentProcess(), affinity)) {
            m_isAffinityChanged = m_cores != ALL_CORES;
            log("[CpuScheduling] Affinity set to %08X", affinity);
        }
        else {
            log("[CpuScheduling] Failed to set the affinity to %08X (%u)", affinity, GetLastError());
        }
    }

    void CpuScheduling::onUI() {
        if (ImGui::TreeNode("CPU Scheduling")) {
            ImGui::TextWrapped("Fixes the client freezing for seconds at a time on CPUs with performance and "
                "efficiency cores (Intel 12th gen and later), without having to set its affinity in Task "
                "Manager every launch.");
            ImGui::Dummy(ImVec2{ 10.0f, 10.0f });

            if (ImGui::Checkbox("Prevent power throttling", &m_preventThrottling)) {
                applyThrottling();
            }

            ImGui::TextWrapped("Stops Windows from slowing the client down to save power, which also moves it "
                "to efficiency cores. Try this first.");
            ImGui::Dummy(ImVec2{ 10.0f, 10.0f });

            if (m_isHybrid) {
                if (ImGui::Combo("Cores", &m_cores, "All cores\0Performance cores only\0Efficiency cores only\0")) {
                    applyAffinity();
                }

                ImGui::TextWrapped("Keeps the client on one kind of core. Performance cores only keeps it on the "
                    "fast cores; efficiency cores only is the same as the Task Manager workaround.");
            }
            else {
                ImGui::TextWrapped("This CPU has one kind of core, so there are no cores to choose.");
            }

            ImGui::TreePop();
        }
    }

    void CpuScheduling::onConfigLoad(const Config& cfg) {
        m_preventThrottling = cfg.get<bool>("CpuScheduling.PreventThrottling").value_or(false);
        m_cores = cfg.get<int>("CpuScheduling.Cores").value_or(ALL_CORES);

        if (m_cores < ALL_CORES || m_cores > EFFICIENCY_CORES) {
            m_cores = ALL_CORES;
        }

        apply();
    }

    void CpuScheduling::onConfigSave(Config& cfg) {
        cfg.set<bool>("CpuScheduling.PreventThrottling", m_preventThrottling);
        cfg.set<int>("CpuScheduling.Cores", m_cores);
    }
}
