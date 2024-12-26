#include <cstdarg>
#include <memory>
#include <vector>

#include <Windows.h>

#include <imgui.h>
#include <String.hpp>

#include "Kanan.hpp"
#include "ChatLog.hpp"

using namespace std;

namespace kanan {
    struct ChatLog {
    public:
        ChatLog(const string& filepath)
            : m_logs{},
            m_filter{},
            m_scrollToBottom{ false },
            m_autoScroll{ true },
            m_file{}
        {
            m_file.open(filepath, std::ios::out | std::ios::app);
            if (m_file.fail())
                return;
            m_file.exceptions(m_file.exceptions() | std::ios::failbit | std::ifstream::badbit);
        }

        void clear() {
            m_logs.clear();
        }

        void addLog(const string& msg) {
            m_logs.push_back(msg);

            m_scrollToBottom = true;


            m_file << msg << std::endl;
        }

        void draw(const string& title, bool* isOpen) {
            ImGui::SetNextWindowSize(ImVec2{ 500.0f, 400.0f }, ImGuiCond_FirstUseEver);

            if (!ImGui::Begin(title.c_str(), isOpen)) {
                ImGui::End();
                return;
            }

            ImGui::Checkbox("Auto Scroll  ", &m_autoScroll);

            ImGui::SameLine();
            m_filter.Draw("Filter", -100.0f);
            ImGui::Separator();
            ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

            if (m_filter.IsActive()) {
                for (auto log : m_logs)
                {
                    if (m_filter.PassFilter(log.c_str())) {
                        ImGui::TextWrappedNoFmt(log.c_str());
                    }
                }
				m_scrollToBottom = false;
            }
            else {
                for (auto log : m_logs)
                {
                    ImGui::TextWrappedNoFmt(log.c_str());
                }
            }

            if (m_scrollToBottom && m_autoScroll) {
                ImGui::SetScrollHere(1.0f);
            }

            m_scrollToBottom = false;

            ImGui::EndChild();
            ImGui::End();
        }

    private:
        ImGuiTextFilter m_filter;
        vector<std::string> m_logs;
        bool m_scrollToBottom;
        bool m_autoScroll;
        ofstream m_file;
    };

    unique_ptr<ChatLog> g_log{};

    void startChatLog(const string& filepath) {
        g_log = make_unique<ChatLog>(filepath);
    }

    void chatLog(const string& msg) {
        g_log->addLog(msg);
    }

    void drawChatLog(bool* isOpen) {
        if (g_log) {
            g_log->draw("Chat Log", isOpen);
        }
    }
}
