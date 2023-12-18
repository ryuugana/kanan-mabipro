#pragma once

#include <fstream>

namespace kanan {
    void startChatLog(const std::string& filepath);

    void chatLog(const std::string& msg);

    void drawChatLog(bool* isOpen);
}
