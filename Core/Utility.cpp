#include <filesystem>
#include <Shlwapi.h>
#include <Windows.h>

#include "miniz.h"
#include "Utility.hpp"
#include "String.hpp"

using namespace std;

namespace kanan {
    bool isKeyDown(int key) {
        return (GetAsyncKeyState(key) & (1 << 15)) != 0;
    }

    bool wasKeyPressed(int key) {
        static bool keys[0xFF]{ false };

        if (isKeyDown(key) && !keys[key]) {
            keys[key] = true;

            return GetActiveWindow() == GetForegroundWindow();
        }

        if (!isKeyDown(key)) {
            keys[key] = false;
        }

        return false;
    }

    string hexify(const uint8_t* data, size_t length) {
        constexpr char hexmap[]{ '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            'a', 'b', 'c', 'd', 'e', 'f' };

        string result{};

        result.resize(length * 2);

        for (size_t i = 0; i < length; ++i) {
            result[2 * i] = hexmap[(data[i] & 0xF0) >> 4];
            result[2 * i + 1] = hexmap[data[i] & 0x0F];
        }

        return result;
    }

    string hexify(const vector<uint8_t>& data) {
        return hexify(data.data(), data.size());
    }

    std::string sha256_file(const std::string& path) {
        HANDLE hFile = CreateFileA(path.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return "Failed to open file " + path;

        HCRYPTPROV hProv;
        HCRYPTHASH hHash;

        // Acquire context and create hash
        if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
            CloseHandle(hFile);
            return "Failed to aquire crypt context";
        }
        if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
            CryptReleaseContext(hProv, 0);
            CloseHandle(hFile);
            return "Failed to create hash";
        }

        BYTE buffer[4096];
        DWORD dwRead;
        while (ReadFile(hFile, buffer, sizeof(buffer), &dwRead, NULL) && dwRead > 0) {
            CryptHashData(hHash, buffer, dwRead, 0);
        }

        DWORD dwHashLen = 32;
        BYTE hashValue[32];
        CryptGetHashParam(hHash, HP_HASHVAL, hashValue, &dwHashLen, 0);

        std::string hexStr = hexify(hashValue, dwHashLen);

        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        CloseHandle(hFile);
        return hexStr;
    }

    // Extracts all files and deletes zip file if successful
    bool unzip_file(const std::string& zipFilePath, const std::string& outputDir) 
    {
        mz_zip_archive zipArchive;
        memset(&zipArchive, 0, sizeof(zipArchive));

        if (!mz_zip_reader_init_file(&zipArchive, zipFilePath.c_str(), 0)) {
            return false;
        }

        int fileCount = (int)mz_zip_reader_get_num_files(&zipArchive);
        for (int i = 0; i < fileCount; ++i) {
            mz_zip_archive_file_stat fileStat;
            if (mz_zip_reader_file_stat(&zipArchive, i, &fileStat)) {
                std::string outputPath = outputDir + "/" + fileStat.m_filename;
                if (!mz_zip_reader_extract_to_file(&zipArchive, i, outputPath.c_str(), 0)) {
                    return false;
                }
            }
        }

        mz_zip_reader_end(&zipArchive);
        
        if (std::filesystem::exists(zipFilePath)) std::filesystem::remove(zipFilePath);

        return true;
    }

    // Application arguments can be passed in directly to appPath
    bool start_application(std::string appPath)
    {
        std::filesystem::path path(appPath);

        PROCESS_INFORMATION processInformation = { 0 };
        STARTUPINFOA startupInfo = { 0 };
        startupInfo.cb = sizeof(STARTUPINFOA);
        BOOL result = CreateProcessA(
            NULL,
            path.u8string().data(),
            NULL,
            NULL,
            FALSE,
            NULL,
            NULL,
            path.parent_path().u8string().data(),
            &startupInfo,
            &processInformation);

        return result;
    }

    // Provide Mabinogi root folder
    bool launch_client(std::string mabiFolderPath)
    {
        std::string clientLaunch = mabiFolderPath;

        clientLaunch.append("\\MabiProLauncher.exe");

        return start_application(clientLaunch);
    }

    // Launch client.exe in the provided Mabi root, forwarding this process's
    // command-line arguments to the launched client.
    bool launch_client_with_args(std::string mabiFolderPath)
    {
        using namespace std;
        filesystem::path rootPath(mabiFolderPath);
        filesystem::path clientPath = rootPath / "client.exe";
        wstring clientPathW = widen(clientPath.u8string());

        // Get the raw command line for this process.
        LPWSTR rawCmd = GetCommandLineW();
        if (!rawCmd) {
            return false;
        }

        // Skip the program name in rawCmd per Windows command-line parsing rules.
        LPWSTR p = rawCmd;
        if (*p == L'"') {
            // Skip quoted program name
            ++p;
            while (*p && *p != L'"') ++p;
            if (*p == L'"') ++p;
        } else {
            // Skip unquoted program name
            while (*p && *p != L' ' && *p != L'\t') ++p;
        }
        // Skip any whitespace after program name.
        while (*p == L' ' || *p == L'\t') ++p;

        // p now points to the first character of the original arguments (or to the terminating NUL).
        wstring trailingArgs;
        if (*p) {
            trailingArgs = p; // preserves original formatting/quoting exactly
        }

        // Build command line: quoted client path followed by original args (if any).
        wstring cmdLine = L"\"";
        cmdLine += clientPathW;
        cmdLine += L"\"";
        if (!trailingArgs.empty()) {
            cmdLine.push_back(L' ');
            cmdLine += trailingArgs;
        }

        // Prepare startup info and working directory.
        STARTUPINFOW startupInfo = { 0 };
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInformation = { 0 };

        wstring cwd = widen(rootPath.u8string());

        // CreateProcessW needs a mutable buffer for lpCommandLine.
        BOOL result = CreateProcessW(
            nullptr,                // lpApplicationName - we're passing full command line instead
            &cmdLine[0],            // lpCommandLine (writable buffer)
            nullptr,                // lpProcessAttributes
            nullptr,                // lpThreadAttributes
            FALSE,                  // bInheritHandles
            0,                      // dwCreationFlags
            nullptr,                // lpEnvironment
            cwd.c_str(),            // lpCurrentDirectory
            &startupInfo,
            &processInformation);

        if (result) {
            CloseHandle(processInformation.hProcess);
            CloseHandle(processInformation.hThread);
        }

        return result != FALSE;
    }
}
