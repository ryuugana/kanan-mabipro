#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <shlwapi.h>
#include <TlHelp32.h>

#include "HttpCore.hpp"
#include "Utility.hpp"

using namespace kanan;

bool IsProcessRunning(const std::string& processName) {
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
    if (snapshot == INVALID_HANDLE_VALUE) return false;

    if (Process32First(snapshot, &entry)) {
        do {
            std::string exeName = entry.szExeFile;
            std::transform(exeName.begin(), exeName.end(), exeName.begin(), ::tolower);
            if (exeName == processName) {
                CloseHandle(snapshot);
                return true;
            }
        } while (Process32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return false;
}

int main()
{
    std::string fileName = "KananMabiPro.zip";
    std::string fileHash = "";

    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    PathRemoveFileSpecA(path); 

    std::string folderPath = path;

    std::string filePath = folderPath;
    filePath.append("\\");
    filePath.append(fileName);

    std::cout << "Waiting for client.exe to close..." << std::endl;

    while (IsProcessRunning("client.exe"))
    {
        Sleep(1000);
    }

    std::cout << "Grabbing Kanan update from release: ";

    std::string updateHash = GetKananReleaseHash(fileName);

    while (updateHash.length() < 1)
    {
        std::cout << "Failed to obtain hash, retrying..." << std::endl;
        Sleep(1000);
        updateHash = GetKananReleaseHash(fileName);
    }

    std::cout << "Obtained hash: " << updateHash.c_str() << std::endl;


    std::cout << "Downloading Kanan update to " << filePath << std::endl;

    std::cout << "Comparing downloaded file hash to actual file hash: ";

    while (updateHash != fileHash)
    {
        if (!fileHash.empty())
        {
            std::cout << "Failed with hash - " << fileHash << std::endl << "Retrying download and verifying hash: ";
            Sleep(1000);
        }
        URLDownloadToFileA(NULL, "https://github.com/ryuugana/kanan-mabipro/releases/latest/download/KananMabiPro.zip", filePath.c_str(), 0, NULL);
        fileHash = sha256_file(filePath);
    }

    std::cout << "Success" << std::endl;

    std::cout << "Extracting update: ";

    if (unzip_file(filePath, path))
    {
        std::cout << "Success" << std::endl;
    }
    else
    {
        std::cout << "Failed" << std::endl;
    }

    std::cout << "Update complete - Relaunching client.exe!" << std::endl;
    if (launch_client(path))
    {
        if (GetLastError() != 0)
        {
            std::cout << "Failed to relaunch patcher. Error: " << GetLastError() << std::endl;
        }
    }

    std::cout << "Waiting before closing to show logging..." << std::endl;
    Sleep(20000);
}