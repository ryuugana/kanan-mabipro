
#include "MabiMessageHook.hpp"
#include <sstream>
#include <ios>
#include <Strsafe.h>
#include <tchar.h>
#include <fcntl.h>
#include <io.h>
#include "Scan.hpp"
#include "Log.hpp"
#include <Patch.hpp>


namespace kanan {
	typedef DWORD(__thiscall* MabiRecvListenerSignature)(MabiMessage mabiMessage);
	const int PACKET_BUFFER_SIZE = 64 * 1024;
	BYTE packetBuffer[PACKET_BUFFER_SIZE];
	bool g_mabiMessageHook = false;
	std::vector<std::unique_ptr<MessageMod>>* mabiRecvListeners = nullptr;

	VOID ListenDownstream(LPVOID Buffer, LONG Size);



	MabiMessageHook::MabiMessageHook(std::vector<std::unique_ptr<MessageMod>>* mabiRecvMods)
	{
		if (g_mabiMessageHook == false) {
			if (DoInjection()) {
				g_mabiMessageHook = true;
				mabiRecvListeners = mabiRecvMods;
				log("MabiMessage hooked successfully.");
			}
			else {
				g_mabiMessageHook = false;
				log("MabiMessage failed to hook.");
			}
		}
	}

	BOOL MabiMessageHook::DoInjection() {
		BOOL result = true;

		log("Initializing MabiMessageHook...");

		log("Patching ReadFromNetworkBuffer...");
		result &= PatchReadFromNetworkBuffer();
		log(result ? "...success" : "...failed");

		log("Finding WriteToNetworkBuffer...");
		result &= FindWriteToNetworkBuffer();
		log(result ? "...success" : "...failed");

		return result;
	}

	//
	// Hook Types & Vars
	//

	typedef DWORD(__thiscall* ReadFromNetworkBufferSignature)(LPVOID MsgPointer, LPVOID Buffer, LONG Size);
	typedef DWORD(__thiscall* WriteToNetworkBufferSignature)(LPVOID MsgPointer, LPVOID Buffer, LONG size);

	ReadFromNetworkBufferSignature ReadFromNetworkBuffer = NULL;
	WriteToNetworkBufferSignature WriteToNetworkBuffer = NULL;

	LPVOID SavedRecvPointer = NULL;
	LONG ReadFromNetworkBufferHookContinueAddress;
	BOOL InsideHookHandler = FALSE;

	//
	// Hook Handlers
	//

	void __stdcall ReadFromNetworkBufferHookHandler(LPVOID MsgPointer, LPVOID Buffer, LONG Size) {
		if (!InsideHookHandler) {
			InsideHookHandler = TRUE;
			ListenDownstream(Buffer, Size);
			InsideHookHandler = FALSE;
			SavedRecvPointer = MsgPointer;
		}
	}

	//
	// Hook Traps
	//

	__declspec(naked) void ReadFromNetworkBufferHookTrap() {
		__asm {
			PUSHAD
			mov     ebp, esp
			mov     eax, [ebp + 32 + 8]
			push    eax
			mov     eax, [ebp + 32 + 4]
			push    eax
			push    ecx
			call    ReadFromNetworkBufferHookHandler
			POPAD

			push    ebp
			mov     ebp, esp
			sub     esp, 28
			push	ebx
			mov		ebx, ecx
			jmp     ReadFromNetworkBufferHookContinueAddress
		}
	}

	template<typename T>
	T swapEndian(T x) {
		static_assert(sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8, "Type must be 2, 4 or 8 bytes to swap endianess");

		if constexpr (sizeof(T) == 2) {
			return (T)_byteswap_ushort((unsigned short)x);
		}
		else if constexpr (sizeof(T) == 4) {
			return (T)_byteswap_ulong((unsigned long)x);
		}
		else {
			return (T)_byteswap_uint64((unsigned long long)x);
		}
	}

	VOID ListenDownstream(LPVOID Buffer, LONG Size) {
		MabiMessage mabiMessage;
		mabiMessage.buffer = (unsigned char*)Buffer;
		mabiMessage.size = Size;
		/*if (recvPacket.GetOP() == 21101) {
			if (strcmp(recvPacket.GetElement(1)->str, "Your skill latency reduction value has been detected to be too high. Please lower it..") == 0) {
				// Remove >skill spam
				memset(Buffer, 0, Size);
				return;
			}
		}*/

		uint32_t op = swapEndian(*(uint32_t*)(Buffer));
		for (uint32_t i = 0; i < mabiRecvListeners->size(); i++) {
			if ((*mabiRecvListeners)[i]->m_isEnabled) {
				if (op == (*mabiRecvListeners)[i]->getOp()) {
					log("op matched %d", mabiMessage.size);
					((MabiRecvListenerSignature)(*mabiRecvListeners)[i]->getFuncPtr())(mabiMessage);
					break;
				}
			}
		}


		std::ostringstream ss{};

//#ifdef DEBUG
		/*try {
			ss << "OP: " << recvPacket.GetOP() << "\n";
			ss << "ID: " << recvPacket.GetReciverId() << "\n";
			for (int i = 0; i < recvPacket.GetElementNum(); i++) {
				switch (recvPacket.GetElement(i)->type) {
				case T_BYTE: ss << "BYTE: " << recvPacket.GetElement(i)->byte8 << "\n"; break;
				case T_SHORT: ss << "SHORT: " << recvPacket.GetElement(i)->word16 << "\n"; break;
				case T_INT: ss << "INT: " << recvPacket.GetElement(i)->int32 << "\n"; break;
				case T_LONG: ss << "LONG: " << recvPacket.GetElement(i)->ID << "\n"; break;
				case T_FLOAT: ss << "FLOAT: " << recvPacket.GetElement(i)->float32 << "\n"; break;
				case T_STRING: ss << "STRING: " << recvPacket.GetElement(i)->str << "\n"; break;
				case T_BIN: ss << "BINARY BLOB: ..." << "\n"; break;
				}
			}
			log("%s\n", ss.str().c_str());
		}
		catch (const char* msg) {
			log(msg);
		}*/
//#endif


	}

	//
	// Patching
	//

	BOOL MabiMessageHook::PatchReadFromNetworkBuffer() {
		std::optional<uintptr_t> ReadFromNetworkBufferFunctionAddressPtr = kanan::scan("Mint.dll", "55 8B EC 83 EC 1C 53 8B D9");
		DWORD ReadFromNetworkBufferFunctionAddress = *ReadFromNetworkBufferFunctionAddressPtr;
		LONG ReadFromNetworkBufferFunctionAddressLong = *(LONG*)(void*)(&ReadFromNetworkBufferFunctionAddress);

		ReadFromNetworkBuffer = (ReadFromNetworkBufferSignature)ReadFromNetworkBufferFunctionAddressLong;
		ReadFromNetworkBufferHookContinueAddress = ReadFromNetworkBufferFunctionAddressLong + 9;

		Hookcall((void*)(ReadFromNetworkBufferFunctionAddressLong), ReadFromNetworkBufferHookTrap, 7);

		Patch m_patch;
		m_patch.address = ReadFromNetworkBufferFunctionAddress;
		m_patch.bytes = { 0xE9 };
		patch(m_patch);
		return TRUE;
	}

	BOOL MabiMessageHook::FindWriteToNetworkBuffer() {
		std::optional<uintptr_t> WriteToNetworkBufferFunctionAddressPtr = kanan::scan("Mint.dll", "55 8B EC 83 EC 18 53 56 8B F1 8B 46 08 57");
		DWORD WriteToNetworkBufferFunctionAddress = *WriteToNetworkBufferFunctionAddressPtr;
		LONG WriteToNetworkBufferFunctionAddressLong = *(LONG*)(void*)(&WriteToNetworkBufferFunctionAddress);
		if (!WriteToNetworkBufferFunctionAddressLong) return FALSE;

		log("WriteToNetworkBuffer prologue address 0x%08X\r", WriteToNetworkBufferFunctionAddress);

		WriteToNetworkBuffer = (WriteToNetworkBufferSignature)WriteToNetworkBufferFunctionAddress;
		return TRUE;
	}

}