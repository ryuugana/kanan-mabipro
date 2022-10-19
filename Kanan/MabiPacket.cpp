//Copyright (c) 2006 some Japanese dude



#include "MabiPacket.h"
#include <windows.h>

#if _MSC_VER >= 1400 //.NET2005
// safe Standard C++ Libraryのchecked_array_iteratorっツーのがあるらしい
// std::copy()で安全なの使えやボッケー言われるから入れておく
#include <iterator>
#endif

#undef ntohs
#undef ntohl
#undef htons
#undef htonl

#define ntohs _ntohs
#define ntohl _ntohl
#define htons _ntohs
#define htonl _ntohl
namespace kanan {
	unsigned short _ntohs(unsigned short s) {
		return ((s & 0xff) << 8) | ((s >> 8) & 0xff);
	}

	unsigned long _ntohl(unsigned long int32) {
		unsigned short hi = (unsigned short)(int32 >> 16);
		unsigned short lo = (unsigned short)(int32 & 0xffff);

		return ((((unsigned long)ntohs(lo)) << 16) | (unsigned long)ntohs(hi));
	}

	__int64 ntohq(__int64 ID) {
		unsigned long t1, t2;
		t1 = static_cast<unsigned long>(ID >> 32);
		t1 = ntohl(t1);

		t2 = static_cast<unsigned long>(ID & 0xffffffff);
		t2 = ntohl(t2);
		ID = t2;
		ID <<= 32;
		ID |= t1;
		return ID;
	}

	__int64 htonq(__int64 q) {
		return ntohq(q);
	}

	unsigned long GetOP(const unsigned char* buf) {
		const MABI_PACKET* head = reinterpret_cast<const MABI_PACKET*>(buf);
		return ntohl(head->cmd);
	}

	__int64 GetReciverID(const unsigned char* buf) {
		const MABI_PACKET* head = reinterpret_cast<const MABI_PACKET*>(buf);
		return ntohq(head->id);
	}



	CMabiPacket::CMabiPacket(void) :op_(0), reciver_id_(0), wc_tmp_buff_(0), wc_tmp_buff_len_(0)
	{
	}

	CMabiPacket::CMabiPacket(unsigned char* buff, int len)
		: op_(0), reciver_id_(0), wc_tmp_buff_(0), wc_tmp_buff_len_(0) {
		SetSource(buff, len);
	}

	CMabiPacket::~CMabiPacket(void)
	{
		ClearElementData();
		delete[] wc_tmp_buff_;
	}

	void CMabiPacket::Clear() {
		ClearElementData();
		op_ = 0;
		reciver_id_ = 0;
	}

	void CMabiPacket::ClearElementData() {
		for (vector<PacketData>::iterator vit = vect_pkt_data_.begin(); vit != vect_pkt_data_.end(); ++vit) {
			switch (vit->type) {
			case 6:
			case 7:
				delete[] vit->str;
				vit->str = NULL;
				break;
			}
		}
		vect_pkt_data_.resize(0);
	}

	void CMabiPacket::AddElement(const PacketData* popd, unsigned int n) {
		PacketData data;

		if (popd == NULL)
			return;

		for (unsigned int i = 0; i < n; ++i) {
			data = popd[i];
			if (data.type == 6 || data.type == 7) {
				data.str = new char[data.len];
				memcpy(data.str, popd[i].str, data.len);
			}
			vect_pkt_data_.push_back(data);
		}
	}

	void CMabiPacket::SetElement(const PacketData* popd, unsigned int n) {
		PacketData data;

		if (popd == NULL)
			return;

		data = popd[0];
		if (data.type == 6 || data.type == 7) {
			data.str = new char[data.len];
			memcpy(data.str, popd[0].str, data.len);
		}

		vect_pkt_data_[n] = data;
}

	void CMabiPacket::SetSource(unsigned char* src, int len) {

		if (len)
			Analyze(src, len);
	}

	void CMabiPacket::Analyze(const unsigned char* src, int len) {
		int n = 0;
		unsigned int data_cnt;

		ClearElementData();

		op_ = kanan::GetOP(src);
		reciver_id_ = kanan::GetReciverID(src);

		n = sizeof(MABI_PACKET);
		if (n == len)
			return;
		n += read_num(src, n);
		n += read_num(src, n, &data_cnt);
		n++;
		vect_pkt_data_.resize(data_cnt);
		for (unsigned int i = 0; i < data_cnt; ++i) {
			n += mp_read(src, &vect_pkt_data_[i], n);
		}

	}

	/* MABI独自表記数値を変換取得
	 * in
	 * unsigned char*:入力
	 * int  :入力オフセット
	 * int* :出力先ポインタ
	 *
	 * out:
	 * 戻り値:変換に使用したバイト数
	*/
	unsigned int CMabiPacket::read_num(const unsigned char* buf, int pos, unsigned int* num) {
		int i = 0;
		unsigned int n = 0;

		buf += pos;
		while (1) {
			n |= ((buf[i] & 0x7f) << (i * 7));
			++i;
			if (!(buf[i - 1] & 0x80))
				break;
		}
		if (num)
			*num = n;
		return i;
	}

	int CMabiPacket::mp_read(const unsigned char* buf, PacketData* pData, int pos) {
		int len;
		PacketData data;

		buf += pos;
		data.int32 = 0;
		data.type = *buf;
		++buf;
		switch (data.type) {
		case 1:
			len = 2;
			data.byte8 = *buf;
			break;
		case 2:
			len = 3;
			data.word16 = ntohs(*(reinterpret_cast<const unsigned short*>(buf)));
			break;
		case 3:
			len = 5;
			data.int32 = ntohl(*(reinterpret_cast<const long*>(buf)));
			break;
		case 4:
			len = 9;
			data.ID = ntohq(*(reinterpret_cast<const __int64*>(buf)));
			break;
		case 5:
			len = 5;
			data.int32 = *(reinterpret_cast<const long*>(buf));
			break;
		case 6: {
			int mb_len;
			int wc_len;
			data.len = ntohs(*(reinterpret_cast<const short*>(buf)));
			len = 3 + data.len;
			buf += 2;
			wc_len = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(buf), data.len, NULL, 0) + 1;
			if (wc_tmp_buff_len_ < wc_len) {
				delete[] wc_tmp_buff_;
				wc_tmp_buff_ = (LPWSTR)new _WCHAR[wc_len];
				wc_tmp_buff_len_ = wc_len;
			}
			MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(buf), data.len, wc_tmp_buff_, wc_len);
			wc_tmp_buff_[wc_len - 1] = 0;
			mb_len = WideCharToMultiByte(0, 0, wc_tmp_buff_, -1, NULL, 0, NULL, NULL) + 1;
			data.str = new char[mb_len];
			WideCharToMultiByte(CP_ACP, 0, wc_tmp_buff_, -1, data.str, mb_len, NULL, NULL);
			data.str[mb_len - 1] = 0;
			data.len = mb_len;
			break;
		}
		case 7:
			data.len = *(reinterpret_cast<const short*>(buf));
			data.len = ntohs(data.len);
			len = 3 + data.len;
			data.str = new char[data.len];
			memcpy(data.str, buf + 2, data.len);
			break;
		default:
			data.type = 0;
			--buf;
			len = 0;
		}
		if (pData != NULL) {
			*pData = data;
		}
		else {
			switch (data.type) {
			case 6:
			case 7:
				delete[] data.str;
			}
		}
		return len;
	}

	int CMabiPacket::BuildPacket(unsigned char** dest, int op, __int64 id, PacketData* pdata, int data_cnt) {
		unsigned char* raw_data_;
		int i;
		int data_len = 0;
		int len = 0;
		int pos = 0;

		*dest = NULL;
		if (op_ == 0 && reciver_id_ == 0 && vect_pkt_data_.size() == 0)
			return 0;

		if (pdata == NULL) {
			data_cnt = 0;
			if (vect_pkt_data_.size()) {
				pdata = &vect_pkt_data_[0];
				data_cnt = static_cast<int>(vect_pkt_data_.size());
			}
		}

		std::vector<unsigned char> op_size;

		for (i = 0; i < data_cnt; ++i) {
			switch (pdata[i].type) {
			case 1:
				data_len += 2;
				break;
			case 2:
				data_len += 3;
				break;
			case 3:
			case 5:
				data_len += 5;
				break;
			case 4:
				data_len += 9;
				break;
			case 6:
			{
				data_len += 3;
				int wc_len;
				// 変換元サイズを指定すると終端NULL分は加算されないので+1
				wc_len = MultiByteToWideChar(CP_ACP, 0, pdata[i].str, pdata[i].len, NULL, 0) + 1;
				if (wc_tmp_buff_len_ < wc_len) {
					delete[] wc_tmp_buff_;
					wc_tmp_buff_ = (LPWSTR)new _WCHAR[wc_len];
					wc_tmp_buff_len_ = wc_len;
				}
				MultiByteToWideChar(CP_ACP, 0, pdata[i].str, pdata[i].len, wc_tmp_buff_, wc_len - 1);
				wc_tmp_buff_[wc_len - 1] = 0;
				//変換元サイズを-1にすると終端NULL分が加算されているので+0
				data_len += WideCharToMultiByte(CP_UTF8, 0, wc_tmp_buff_, -1, NULL, 0, NULL, NULL);
				break;
			}
			case 7:
				data_len += 3 + pdata[i].len;
				break;
			}
		}
		if (data_len) {
			i = data_len;
			while (i) {
				if (i > 0x7f)
					op_size.push_back(0x80 | (i & 0xff));
				else
					op_size.push_back(i & 0xff);
				i >>= 7;
			}
			i = data_cnt;
			while (i) {
				if (i > 0x7f)
					op_size.push_back(0x80 | (i & 0xff));
				else
					op_size.push_back(i & 0xff);
				i >>= 7;
			}
			op_size.push_back(0);
		}
		else {
			op_size.push_back(0);
			op_size.push_back(0);
			op_size.push_back(0);
		}

		len = 4 + 8 + static_cast<int>(op_size.size()) + data_len;

		*dest = new unsigned char[len];
		raw_data_ = *dest;

		pos = 0;
		*(reinterpret_cast<long*>(raw_data_)) = htonl(op);
		pos += 4;
		*(reinterpret_cast<__int64*>(raw_data_ + pos)) = htonq(id);
		pos += 8;
#if _MSC_VER >= 1400
		std::copy(op_size.begin(), op_size.end(), stdext::checked_array_iterator<unsigned char*>(raw_data_ + pos, len - pos));
#else
		std::copy(op_size.begin(), op_size.end(), raw_data_ + pos);
#endif
		pos += static_cast<int>(op_size.size());

		for (i = 0; i < data_cnt; ++i) {
			*(raw_data_ + pos) = pdata[i].type;
			++pos;
			switch (pdata[i].type) {
			case 1:
				*(raw_data_ + pos) = pdata[i].byte8;
				pos += 1;
				break;
			case 2:
				*(reinterpret_cast<unsigned short*>(raw_data_ + pos)) = htons(pdata[i].word16);
				pos += 2;
				break;
			case 3:
				*(reinterpret_cast<unsigned long*>(raw_data_ + pos)) = htonl(pdata[i].int32);
				pos += 4;
				break;
			case 5:
				*(reinterpret_cast<unsigned long*>(raw_data_ + pos)) = pdata[i].int32;
				pos += 4;
				break;
			case 4:
				*(reinterpret_cast<__int64*>(raw_data_ + pos)) = htonq(pdata[i].ID);
				pos += 8;
				break;
			case 6:
			{
				char* utf8;
				int utf_len;
				int wc_len;
				if (*(pdata[i].str) == 0) {
					*(raw_data_ + pos) = 0;
					*(raw_data_ + pos + 1) = 1;
					*(raw_data_ + pos + 2) = 0;
					pos += 3;
				}
				else {
					wc_len = MultiByteToWideChar(CP_ACP, 0, pdata[i].str, pdata[i].len, NULL, 0) + 1;
					if (wc_tmp_buff_len_ < wc_len) {
						delete[] wc_tmp_buff_;
						wc_tmp_buff_ = (LPWSTR)new _WCHAR[wc_len];
						wc_tmp_buff_len_ = wc_len;
					}
					MultiByteToWideChar(CP_ACP, 0, pdata[i].str, pdata[i].len, wc_tmp_buff_, wc_len);
					wc_tmp_buff_[wc_len - 1] = 0;
					utf_len = WideCharToMultiByte(CP_UTF8, 0, wc_tmp_buff_, -1, NULL, 0, NULL, NULL);
					utf8 = new char[utf_len];
					WideCharToMultiByte(CP_UTF8, 0, wc_tmp_buff_, -1, utf8, utf_len, NULL, NULL);
					utf8[utf_len - 1] = 0;
					*((unsigned short*)(raw_data_ + pos)) = htons(utf_len);
					pos += 2;
					memcpy(raw_data_ + pos, utf8, utf_len);
					delete[] utf8;
					pos += utf_len;
				}
				break;
			}
			case 7:
				*(reinterpret_cast<short*>(raw_data_ + pos)) = htons(pdata[i].len);
				pos += 2;
				memcpy(raw_data_ + pos, pdata[i].str, pdata[i].len);
				pos += pdata[i].len;
				break;
			}
		}

		return len;

	}

	void CMabiPacket::FreeElement(unsigned int n) {
		if (n >= vect_pkt_data_.size())
			return;
		std::vector<PacketData>::iterator vit = vect_pkt_data_.begin() + n;
		switch (vit->type) {
		case 6:
		case 7:
			delete[] vit->str;
		}
		vect_pkt_data_.erase(vit);
	}

	__int64 CMabiPacket::GetReciverId() const
	{
		return reciver_id_;
	}
}