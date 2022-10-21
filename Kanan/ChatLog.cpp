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

            /*if (ImGui::Button("Clear")) {
                clear();
            }*/

            ImGui::SameLine();

            auto copy = ImGui::Button("Copy Logs");

            ImGui::SameLine();
            m_filter.Draw("Filter", -100.0f);
            ImGui::Separator();
            ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

            if (copy) {
                ImGui::LogToClipboard();
            }

            if (m_filter.IsActive()) {
                for (auto log : m_logs)
                {
                    if (m_filter.PassFilter(log.c_str())) {
                        ImGui::TextWrapped(log.c_str());
                    }
                }
            }
            else {
                for (auto log : m_logs)
                {
                    ImGui::TextWrapped(log.c_str());
                }
            }

            if (m_scrollToBottom) {
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
        ofstream m_file;
    };

    unique_ptr<ChatLog> g_log{};

    void startChatLog(const string& filepath) {
        g_log = make_unique<ChatLog>(filepath);
    }

    void chatLog(const string& msg) {
        g_log->addLog(msg);
    }

    void chatMsg(const string& msg) {
        chatLog(msg);

        // Use the real window if we have it.
        HWND wnd{ nullptr };

        if (g_kanan) {
            wnd = g_kanan->getWindow();
        }
        else {
            wnd = GetDesktopWindow();
        }

        MessageBox(wnd, widen(msg).c_str(), L"Kanan", MB_ICONINFORMATION | MB_OK);
    }

    void chatError(const string& msg) {
        chatLog(msg);

        // Use the real window if we have it.
        HWND wnd{ nullptr };

        if (g_kanan) {
            wnd = g_kanan->getWindow();
        }
        else {
            wnd = GetDesktopWindow();
        }

        MessageBox(wnd, widen(msg).c_str(), L"Kanan Error!", MB_ICONERROR | MB_OK);
    }

    void chatLog(const char* format, ...) {
        va_list args{};

        va_start(args, format);
        chatLog(formatString(format, args));
        va_end(args);
    }

    void chatMsg(const char* format, ...) {
        va_list args{};

        va_start(args, format);
        chatMsg(formatString(format, args));
        va_end(args);
    }

    void chatError(const char* format, ...) {
        va_list args{};

        va_start(args, format);
        chatError(formatString(format, args));
        va_end(args);
    }

    void drawChatLog(bool* isOpen) {
        if (g_log) {
            g_log->draw("Chat Log", isOpen);
        }
    }
}
