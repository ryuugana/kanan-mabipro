#include "String.hpp"
#include "Module.hpp"
#include <TlHelp32.h>
#include "Process.hpp"

using namespace std;

namespace kanan {
    optional<size_t> getModuleSize(const string& module) {
        return getModuleSize(GetModuleHandle(widen(module).c_str()));
    }

    optional<size_t> getModuleSize(HMODULE module) {
        if (module == nullptr) {
            return {};
        }

        // Get the dos header and verify that it seems valid.
        auto dosHeader = (PIMAGE_DOS_HEADER)module;

        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
            return {};
        }

        // Get the nt headers and verify that they seem valid.
        auto ntHeaders = (PIMAGE_NT_HEADERS)((uintptr_t)dosHeader + dosHeader->e_lfanew);

        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
            return {};
        }

        // OptionalHeader is not actually optional.
        return ntHeaders->OptionalHeader.SizeOfImage;
    }

    optional<uintptr_t> getModuleBase(const string& ModuleName)
    {
        MODULEENTRY32 ModuleEntry = { 0 };
        HANDLE SnapShot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, *getProcessID("client.exe"));

        if (!SnapShot) return NULL;

        ModuleEntry.dwSize = sizeof(ModuleEntry);

        if (!Module32First(SnapShot, &ModuleEntry)) return NULL;

        do
        {
            if (!wcscmp(ModuleEntry.szModule, widen(ModuleName).c_str()))
            {
                CloseHandle(SnapShot);
                return (uintptr_t)ModuleEntry.modBaseAddr;
            }
        } while (Module32Next(SnapShot, &ModuleEntry));

        CloseHandle(SnapShot);
        return NULL;
    }

    optional<uintptr_t> ptrFromRVA(uint8_t* dll, uintptr_t rva) {
        // Get the first section.
        auto dosHeader = (PIMAGE_DOS_HEADER)&dll[0];
        auto ntHeaders = (PIMAGE_NT_HEADERS)&dll[dosHeader->e_lfanew];
        auto section = IMAGE_FIRST_SECTION(ntHeaders);

        // Go through each section searching for where the rva lands.
        for (uint16_t i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i, ++section) {
            auto size = section->Misc.VirtualSize;

            if (size == 0) {
                size = section->SizeOfRawData;
            }

            if (rva >= section->VirtualAddress && rva < (section->VirtualAddress + size)) {
                auto delta = section->VirtualAddress - section->PointerToRawData;

                return (uintptr_t)(dll + (rva - delta));
            }
        }

        return {};
    }
}
