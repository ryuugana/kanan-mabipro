#include <cstdarg>
#include <memory>
#include <mutex>
#include <vector>

#include <Windows.h>

#include <imgui.h>
#include <String.hpp>

#include "Kanan.hpp"
#include "Log.hpp"

using namespace std;

namespace kanan {
    struct Log {
    public:
        Log(const string& filepath) 
            : m_buf{},
            m_filter{},
            m_lineOffsets{},
            m_scrollToBottom{ false },
            m_autoScroll { true },
            m_file{}
        {
            m_file.open(filepath);

            if (!m_file.is_open()) {
                error("Failed to open log file: %s!", filepath.c_str());
            }
        }

        void clear() {
            lock_guard _{ m_mutex };

            m_buf.clear();
            m_lineOffsets.clear();
        }

        void addLog(const string& msg)  {
            add(msg, "%s\n");
        }

        void addString(const string& msg) {
            add(msg, "%s");
        }

        void draw(const string& title, bool* isOpen) {
            lock_guard _{ m_mutex };

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

            ImGui::SameLine();

            if (ImGui::Button("Clear")) {
                clear();
            }

            ImGui::SameLine();

            auto copy = ImGui::Button("Copy");

            if (copy) {
                ImGui::LogToClipboard();
            }

            if (m_filter.IsActive()) {
                auto bufBegin = m_buf.begin();
                auto line = bufBegin;

                for (auto lineNum = 0; line != nullptr; ++lineNum) {
                    auto lineEnd = (lineNum < m_lineOffsets.Size) ? bufBegin + m_lineOffsets[lineNum] : nullptr;

                    if (m_filter.PassFilter(line, lineEnd)) {
                        ImGui::TextUnformatted(line, lineEnd);
                    }

                    line = lineEnd && lineEnd[1] ? lineEnd + 1 : nullptr;
                }
            }
            else {
                ImGui::TextUnformatted(m_buf.begin());
            }

            if (m_scrollToBottom && m_autoScroll) {
                ImGui::SetScrollHereY(1.0f);
            }

            m_scrollToBottom = false;

            ImGui::EndChild();
            ImGui::End();
        }

    private:
        // The log window keeps about this much of the log; kananLog.txt keeps all of it.
        static constexpr int MAX_BUFFER_SIZE = 1024 * 1024;

        // Kanan logs from more than one thread (its setup, the game's frames, the update check).
        recursive_mutex m_mutex;
        ImGuiTextBuffer m_buf;
        ImGuiTextFilter m_filter;
        ImVector<int> m_lineOffsets; // Index to lines offset
        bool m_scrollToBottom;
        bool m_autoScroll;
        ofstream m_file;

        void add(const string& msg, const char* format) {
            lock_guard _{ m_mutex };

            if (m_buf.size() + (int)msg.size() > MAX_BUFFER_SIZE) {
                trim();
            }

            auto oldSize = m_buf.size();

            m_buf.appendf(format, msg.c_str());

            for (auto newSize = m_buf.size(); oldSize < newSize; ++oldSize) {
                if (m_buf[oldSize] == '\n') {
                    m_lineOffsets.push_back(oldSize);
                }
            }

            m_scrollToBottom = true;

            // Flushed with each line so the log is complete if the game crashes.
            if (m_file.is_open()) {
                m_file << msg << endl;
            }
        }

        // Drops the older half of the lines kept for the log window.
        void trim() {
            if (m_lineOffsets.Size < 2) {
                m_buf.clear();
                m_lineOffsets.clear();
                return;
            }

            auto cut = m_lineOffsets[m_lineOffsets.Size / 2] + 1;
            string rest{ m_buf.begin() + cut, m_buf.end() };

            m_buf.clear();
            m_lineOffsets.clear();
            m_buf.append(rest.c_str(), rest.c_str() + rest.size());

            for (auto i = 0; i < m_buf.size(); ++i) {
                if (m_buf[i] == '\n') {
                    m_lineOffsets.push_back(i);
                }
            }
        }
    };

    unique_ptr<Log> g_log{};

    void startLog(const string& filepath) {
        g_log = make_unique<Log>(filepath);
    }

    void log(const string& msg) {
        g_log->addLog(msg);
    }

    void logString(const string& msg) {
        g_log->addString(msg);
    }

    void msg(const string& msg) {
        log(msg);

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

    void error(const string& msg) {
        log(msg);

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

    void log(const char* format, ...) {
        va_list args{};

        va_start(args, format);
        log(formatString(format, args));
        va_end(args);
    }

    void logNoNewLine(const char* format, ...) {
        va_list args{};

        va_start(args, format);
        logString(formatString(format, args));
        va_end(args);
    }

    void msg(const char* format, ...) {
        va_list args{};

        va_start(args, format);
        msg(formatString(format, args));
        va_end(args);
    }

    void error(const char* format, ...) {
        va_list args{};

        va_start(args, format);
        error(formatString(format, args));
        va_end(args);
    }

    void drawLog(bool* isOpen) {
        if (g_log) {
            g_log->draw("Log", isOpen);
        }
    }
}
