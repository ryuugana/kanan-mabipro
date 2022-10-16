#pragma once

#include <SDKDDKVer.h>

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers

// Header Files:
#include <windows.h>
#include <stdint.h>
#include <Shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")


namespace kanan {
	class MabiMessageHook {
	public:
		MabiMessageHook();

		BOOL addRecvListener(void* funcPtr, uint32_t op);

	private:
		//
		// Hook
		//

		BOOL DoInjection();
		BOOL PatchReadFromNetworkBuffer();
		BOOL FindWriteToNetworkBuffer();
	};
}