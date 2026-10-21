#include <string>

#include <Windows.h>
#include <shellapi.h>

#include <imgui.h>
#include <String.hpp>

#include "Log.hpp"
#include "MabiWiki.hpp"

#pragma comment(lib, "shell32.lib")

using namespace std;

namespace kanan {
    static MabiWiki* g_mabiWiki{ nullptr };

    // The wiki's page, shipped apart from Kanan.
    static const wchar_t* PAGE_NAME = L"MabiWiki_Standalone.html";

    // The folder a file is in, with its "\".
    static wstring folderOf(const wstring& path) {
        return path.substr(0, path.find_last_of(L'\\') + 1);
    }

    // The game folder: the exe's.
    static wstring gameFolder() {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);

        return folderOf(exe);
    }

    // Kanan.asi's folder (system\mss\ in the game folder).
    static wstring kananFolder() {
        HMODULE module{};

        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)&kananFolder, &module)) {
            return L"";
        }

        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(module, path, MAX_PATH);

        return folderOf(path);
    }

    wstring MabiWiki::pagePath() {
        for (auto& folder : { gameFolder(), kananFolder() }) {
            auto path = folder + PAGE_NAME;

            if (!folder.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
                return path;
            }
        }

        return L"";
    }

    // Opens a file through Explorer, so the browser doesn't start with the game's administrator
    // rights. By its full path: a bare explorer.exe is looked for in the current folder (the
    // game's) first.
    static bool openInExplorer(const wstring& path) {
        wchar_t windows[MAX_PATH]{};
        auto length = GetWindowsDirectoryW(windows, MAX_PATH);
        auto explorer = (length > 0 && length < MAX_PATH ? wstring{ windows } + L"\\" : wstring{}) + L"explorer.exe";
        auto quoted = L"\"" + path + L"\"";
        auto result = (intptr_t)ShellExecuteW(nullptr, L"open", explorer.c_str(), quoted.c_str(), nullptr, SW_SHOWNORMAL);

        log("[MabiWiki] Opening %S: %d", path.c_str(), (int)result);

        return result > 32;
    }

    MabiWiki::MabiWiki()
        : m_openStatus{}
    {
        g_mabiWiki = this;
    }

    MabiWiki::~MabiWiki() {
        if (g_mabiWiki == this) {
            g_mabiWiki = nullptr;
        }
    }

    MabiWiki* MabiWiki::instance() {
        return g_mabiWiki;
    }

    bool MabiWiki::open(wstring& error) {
        auto path = pagePath();

        if (path.empty()) {
            error = L"MabiWiki_Standalone.html wasn't found. Put it in your Mabinogi folder.";
            return false;
        }

        if (!openInExplorer(path)) {
            error = L"Couldn't open " + path;
            return false;
        }

        return true;
    }

    void MabiWiki::onUI() {
        if (ImGui::TreeNode("Mabinogi Wiki")) {
            ImGui::TextWrapped(
                "A wiki of the game's items, enchants, skills, monsters and more, made from your game's own files. "
                "It's the page MabiWiki_Standalone.html: put it in your Mabinogi folder. It opens in your browser and "
                "works offline; you can also type .wiki in chat (with Chat Commands on)."
            );
            ImGui::Spacing();

            if (ImGui::Button("Open Mabinogi Wiki")) {
                wstring error;
                m_openStatus = open(error) ? "Opened in your browser."
                    : pagePath().empty() ? "MabiWiki_Standalone.html wasn't found in your Mabinogi folder." : narrow(error);
            }

            if (!m_openStatus.empty()) {
                ImGui::TextWrapped("%s", m_openStatus.c_str());
            }

            ImGui::TreePop();
        }
    }
}
