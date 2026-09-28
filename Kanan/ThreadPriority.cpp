#include <Windows.h>

#include <imgui.h>

#include "Log.hpp"
#include "ThreadPriority.hpp"

namespace kanan {
    static const char* const g_choiceNames[] = { "Game default", "Above normal", "High" };

    ThreadPriority::ThreadPriority()
        : m_choice{ GAME_DEFAULT },
        m_appliedChoice{ GAME_DEFAULT },
        m_originalPriority{ THREAD_PRIORITY_NORMAL },
        m_isOriginalKnown{ false }
    {
    }

    // Frames are drawn on the game's main thread, so this is where its priority is set.
    void ThreadPriority::onFrame() {
        if (m_choice == m_appliedChoice) {
            return;
        }

        auto thread = GetCurrentThread();

        if (!m_isOriginalKnown) {
            m_originalPriority = GetThreadPriority(thread);
            m_isOriginalKnown = true;
        }

        int priority{};

        switch (m_choice) {
        case ABOVE_NORMAL: priority = THREAD_PRIORITY_ABOVE_NORMAL; break;
        case HIGH: priority = THREAD_PRIORITY_HIGHEST; break;
        default: priority = m_originalPriority; break;
        }

        if (SetThreadPriority(thread, priority)) {
            log("[ThreadPriority] Main thread priority set to %s (%d)", g_choiceNames[m_choice], priority);
        }
        else {
            log("[ThreadPriority] Failed to set the main thread's priority (error %lu)", GetLastError());
        }

        m_appliedChoice = m_choice;
    }

    void ThreadPriority::onUI() {
        if (ImGui::TreeNode("Thread Priority")) {
            ImGui::TextWrapped("Raises the priority of the game's main thread, the one that runs the game and draws "
                "it, so Windows runs it first when other programs want the same processor. This can smooth out "
                "hitches while other programs are busy, at the cost of those programs getting less time while "
                "the game is.");
            ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
            ImGui::Combo("Priority", &m_choice, g_choiceNames, CHOICE_COUNT);
            ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
            ImGui::TextWrapped("High is what AstralWorld used. It works alongside CPU Scheduling, which picks "
                "which processors the game runs on; this picks how soon it runs on them.");
            ImGui::TreePop();
        }
    }

    void ThreadPriority::onConfigLoad(const Config& cfg) {
        m_choice = cfg.get<int>("ThreadPriority.Choice").value_or(GAME_DEFAULT);

        if (m_choice < GAME_DEFAULT || m_choice >= CHOICE_COUNT) {
            m_choice = GAME_DEFAULT;
        }
    }

    void ThreadPriority::onConfigSave(Config& cfg) {
        cfg.set<int>("ThreadPriority.Choice", m_choice);
    }
}
