#pragma once

#include <fstream>

namespace kanan {
    void startChatLog(const std::string& filepath);

    void chatLog(const char* format, ...);
    void chatMsg(const char* format, ...);
    void chatError(const char* format, ...);

    void drawChatLog(bool* isOpen);
}
