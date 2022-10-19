//Copyright (c) 2006 some Japanese dude


#pragma once

#define __MABIPACKET_H__
#include <vector>
#include "windows.h"

using namespace std;
namespace kanan {
	enum MessageElementType : int {
		T_NONE = 0,
		T_BYTE = 1,
		T_SHORT = 2,
		T_INT = 3,
		T_LONG = 4,
		T_FLOAT = 5,
		T_STRING = 6,
		T_BIN = 7
	};

	typedef unsigned short _WCHAR;

	/* パケのヘッダー固定部分の定義
	 * このヘッダの後ろにcmdに応じてデータがあるので解析すると情報が取れる
	 * なお、データはすべてネットワークバイトオーダーに変換する/されているので注意すること
	 */
#pragma pack(push)
#pragma pack(1)
	typedef struct MABI_PACKET {
		unsigned long	cmd;
		__int64			id;
	}MABI_PACKET;

	typedef struct {
		unsigned char type;
		union {
			unsigned int int32;
			unsigned short word16;
			unsigned char byte8;
			float float32;
			char* str;
			__int64 ID;
		};
		int len;
	}PacketData;

#pragma pack(pop)

	class CMabiPacket
	{
	public:
		CMabiPacket(void);
		CMabiPacket(unsigned char*, int);
		~CMabiPacket(void);
		void SetSource(unsigned char*, int);
		int BuildPacket(unsigned char** dest, int op, __int64 id, PacketData* pop = NULL, int op_num = 0);
		int BuildPacket(unsigned char** dest) { return BuildPacket(dest, op_, reciver_id_); }
		void FreePacket(unsigned char* p) { delete[] p; }
		void AddElement(const PacketData*, unsigned int n = 1);
		const PacketData* GetElement(unsigned int n)const { return &vect_pkt_data_.at(n); }
		void SetElement(const PacketData*, unsigned int n);
		PacketData* GetElement(unsigned int n) { return &vect_pkt_data_.at(n); }
		int GetElementNum() const { return static_cast<int>(vect_pkt_data_.size()); }
		void FreeElement(unsigned int n);
		int GetOP() const { return op_; }
		void SetOP(int op) { op_ = op; }
		void SetReciverId(__int64 id) { reciver_id_ = id; }
		__int64 GetReciverId() const;
		void Clear();
		void ClearElementData();

	private:
		void Analyze(const unsigned char* src, int len);
		unsigned int read_num(const unsigned char*, int pos = 0, unsigned int* num = NULL);
		int mp_read(const unsigned char* buf, PacketData* op, int pos = 0);

		unsigned int op_;
		vector<PacketData> vect_pkt_data_;
		__int64 reciver_id_;
		LPWSTR wc_tmp_buff_;
		int wc_tmp_buff_len_;

	};

	//CMabiPacketを使わずにOPを取得する時に使う
	unsigned long GetOP(const unsigned char*);
	__int64 GetReciverID(const unsigned char*);
	__int64 htonq(__int64);
	__int64 ntohq(__int64);
}