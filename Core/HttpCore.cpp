#include <windows.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <iostream>
#include <string>
#include <vector>

#include "HttpCore.hpp"
#include "json.hpp"

#pragma comment(lib, "winhttp.lib")

using nlohmann::json;

namespace kanan
{
    std::string winHttpGet(const wchar_t* host, const wchar_t* path, DWORD port) {
        std::string result;
        HINTERNET hSession = WinHttpOpen(L"KananCore", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return result;

        HINTERNET hConnect = WinHttpConnect(hSession, host, port, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return result; }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, NULL,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return result;
        }

        // Optional: add auth header
        // std::wstring header = L"Authorization: Bearer YOUR_TOKEN\r\n";
        // WinHttpAddRequestHeaders(hRequest, header.c_str(), (DWORD)header.size(), WINHTTP_ADDREQ_FLAG_ADD);

        WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0);
        WinHttpReceiveResponse(hRequest, NULL);

        DWORD dwSize = 0;
        do {
            dwSize = 0;
            WinHttpQueryDataAvailable(hRequest, &dwSize);
            if (dwSize == 0) break;

            std::vector<char> buffer(dwSize);
            DWORD dwDownloaded = 0;
            WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded);
            result.append(buffer.data(), dwDownloaded);
        } while (dwSize > 0);

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    std::string GetKananRelease() {
        // Fetch GitHub API for latest release
        std::string json = winHttpGet(L"api.github.com", L"/repos/ryuugana/kanan-mabipro/releases/latest");
        if (json.empty()) {
            std::cout << "Request failed." << std::endl;
            return "";
        }

        return json;
    }

    std::string GetKananReleaseHash() {
        std::string hash = "";
        try
        {
            std::string kananReleaseString = GetKananRelease();
            json j = json::parse(kananReleaseString);

            std::cout << j.at("name").get<std::string>() << std::endl;

            for (const auto& asset : j.at("assets")) {
                if (asset.at("name").get<std::string>() == "KananMabiPro.zip")
                {
                    hash = asset.at("digest").get<std::string>();
                }
            }
        }
        catch (const json::parse_error& e)
        {
            std::cout << "Failed to get Kanan release hash: " << e.what() << std::endl;
        }

        if (hash.length() > 0)
        {
            return hash.substr(hash.find(':') + 1);
        }

        return hash;
    }
}