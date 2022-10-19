#pragma once

#include <SDKDDKVer.h>

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers

// Header Files:
#include <windows.h>
#include <stdint.h>
#include <vector>

#include "MessageMod.hpp"


namespace kanan {
	class MabiMessageHook {
	public:
		MabiMessageHook(std::vector<std::unique_ptr<MessageMod>>* mabiRecvMods);

	private:
		//
		// Hook
		//

		BOOL DoInjection();
		BOOL PatchReadFromNetworkBuffer();
		BOOL FindWriteToNetworkBuffer();
	};
}