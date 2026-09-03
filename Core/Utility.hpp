#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kanan {
    bool isKeyDown(int key);
    bool wasKeyPressed(int key);

    std::string hexify(const uint8_t* data, size_t length);
    std::string hexify(const std::vector<uint8_t>& data);

    std::string sha256_file(const std::string& path);

    bool unzip_file(const std::string& zipFilePath, const std::string& outputDir);

    bool start_application(std::string appPath);
    bool launch_client(std::string mabiFolderPath);

    // Given the address of a relative offset, calculate the absolute address.
    constexpr uintptr_t rel_to_abs(uintptr_t address, int offset = 4) {
        auto rel = *(int*)(address);
        auto ip = address + offset;

        return ip + rel;
    }
}
