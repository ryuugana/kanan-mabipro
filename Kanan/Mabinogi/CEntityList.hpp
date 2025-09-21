#pragma once
class CItem;
class CCharacter;
#pragma pack(push, 1)
class CEntityList {
public:
    class CItemList {
    public:
        class CCItemListNode {
        public:
        }; // Size: 0x0

        class CItemListNode {
        public:
            class CItemListNodeEntry {
            public:
                char pad_0[0x10];
                CItem* item; // 0x10
            }; // Size: 0x14

            CItemListNodeEntry* entry; // 0x0
            CEntityList::CItemList::CItemListNode* next; // 0x4
        }; // Size: 0x8

        char pad_0[0x4];
        CItemListNode* root; // 0x4
        uint32_t count; // 0x8
    }; // Size: 0xc

    class CCharacterList {
    public:
        class CCharacterListNode {
        public:            
            CEntityList::CCharacterList::CCharacterListNode* next; // 0x0
            char pad_0[0xC]; // 0x4
            CCharacter* character; // 0x10
        }; // Size: 0x10

        CCharacterListNode* root; // 0x0
        char pad_0[0x10];
        uint32_t count; // 0x8
    }; // Size: 0xc

    //char pad_0[0x8];
    //CItemList items; // 0x8
    //char pad_14[0x14];


    CCharacterList characters; // 0x0
}; // Size: ?
#pragma pack(pop)
