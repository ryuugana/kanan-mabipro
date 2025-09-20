#pragma once
#pragma pack(push, 1)
class CWorld {
public:
    char pad_0[0xb0];
    uint64_t localPlayerID; // 0xb0
}; // Size: 0xc0
#pragma pack(pop)
