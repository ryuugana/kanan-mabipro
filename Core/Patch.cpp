#include <Windows.h>

#include "Patch.hpp"

using namespace std;

namespace kanan {
    bool patch(uintptr_t address, const vector<int16_t>& bytes) {
        auto oldProtection = protect(address, bytes.size(), PAGE_EXECUTE_READWRITE);

        if (!oldProtection) {
            return false;
        }

        unsigned int count = 0;

        for (auto byte : bytes) {
            if (byte >= 0 && byte <= 0xFF) {
                *(uint8_t*)(address + count) = (uint8_t)byte;
            }

            ++count;
        }

        FlushInstructionCache(GetCurrentProcess(), (LPCVOID)address, bytes.size());
        protect(address, bytes.size(), *oldProtection);

        return true;
    }

    bool patch(Patch& p) {
        if (p.address == 0 || p.bytes.empty()) {
            return false;
        }

        // Backup the original bytes.
        if (p.originalBytes.empty()) {
            p.originalBytes.resize(p.bytes.size());

            unsigned int count = 0;

            for (auto& byte : p.originalBytes) {
                byte = *(uint8_t*)(p.address + count++);
            }
        }

        // Apply the patch.
        return patch(p.address, p.bytes);
    }

    bool undoPatch(const Patch& p) {
        if (p.address == 0 || p.originalBytes.empty()) {
            return false;
        }

        // Patch in the original bytes.
        return patch(p.address, p.originalBytes);
    }

	bool hookPatch(Patch& p)  {
		if (p.address == 0 || p.funcAddress == 0 || !p.bytes.empty() || p.type == P_NONE) {
			return false;
		}

		// Backup the original bytes.
		if (p.originalBytes.empty()) {
			p.originalBytes.resize(p.size);

			unsigned int count = 0;

			for (auto& byte : p.originalBytes) {
				byte = *(uint8_t*)(p.address + count++);
			}
		}

		// Apply the patch.
		if (p.type == P_CALL) {
			return Hookcall((void*)p.address, p.funcAddress, p.size);
		}
		else if (p.type == P_JMP) {
			return Hookjmp((void*)p.address, p.funcAddress, p.size);
		}

		return false;
	}

	bool Hookcall(void* toHook, void* ourFunct, int len)
	{
		if (len < 5)
		{
			return false;
		}
		DWORD curProtection;
		VirtualProtect(toHook, len, PAGE_EXECUTE_READWRITE, &curProtection);
		memset(toHook, 0x90, len);

		DWORD relativeAddress = ((DWORD)ourFunct - (DWORD)toHook) - 5;
		*(BYTE*)toHook = 0xE8;
		*(DWORD*)((DWORD)toHook + 1) = relativeAddress;

		DWORD temp;
		VirtualProtect(toHook, len, curProtection, &temp);

		return true;
	}

	bool Hookjmp(void* toHook, void* ourFunct, int len)
	{
		if (len < 5)
		{
			return false;
		}
		DWORD curProtection;
		VirtualProtect(toHook, len, PAGE_EXECUTE_READWRITE, &curProtection);
		memset(toHook, 0x90, len);

		DWORD relativeAddress = ((DWORD)ourFunct - (DWORD)toHook) - 5;
		*(BYTE*)toHook = 0xE9;
		*(DWORD*)((DWORD)toHook + 1) = relativeAddress;

		DWORD temp;
		VirtualProtect(toHook, len, curProtection, &temp);

		return true;
	}

    optional<DWORD> protect(uintptr_t address, size_t size, DWORD protection) {
        DWORD oldProtection{ 0 };

        if (VirtualProtect((LPVOID)address, size, protection, &oldProtection) != FALSE) {
            return oldProtection;
        }

        return {};
    }
}
