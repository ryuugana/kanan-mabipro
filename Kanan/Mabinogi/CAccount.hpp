#pragma once
#pragma pack(push, 1)
class CAccount {
public:
    char pad_0[0xD8];
    uint64_t localPlayerID; // 0xD8
}; // Actual Size: 0x170
#pragma pack(pop)
