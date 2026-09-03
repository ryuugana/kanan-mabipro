#pragma once

#include <cstdint>
#include <winhttp.h>

namespace kanan {
    std::string winHttpGet(const wchar_t* host, const wchar_t* path, DWORD port = INTERNET_DEFAULT_HTTPS_PORT);
    std::string GetKananRelease();
    std::string GetKananReleaseHash(std::string assetName);
}
