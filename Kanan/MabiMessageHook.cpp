
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
#include "MabiPacket.h"
#include <thread>


namespace kanan {
	const int PACKET_BUFFER_SIZE = 64 * 1024;
	BYTE packetBuffer[PACKET_BUFFER_SIZE];
	bool g_mabiMessageHook = false;
	std::vector<std::unique_ptr<MessageMod>>* mabiListeners = nullptr;
	std::vector<MabiMessage> mabiMessagesR;
	std::vector<MabiMessage> mabiMessagesS;
	unsigned int mintAddress = NULL;
	unsigned int pleioneAddress = NULL;

	VOID ListenDownstream(LPVOID Buffer, LONG Size);
	VOID ListenUpstream(LPVOID Buffer, LONG Size);
	VOID InjectRecvQueue();
	VOID InjectSendQueue();
	extern "C" int Recv(BYTE* buffer, unsigned int size);
	extern "C" int Send(BYTE* buffer, unsigned int size);



	MabiMessageHook::MabiMessageHook(std::vector<std::unique_ptr<MessageMod>>* mabiMods)
	{
		if (g_mabiMessageHook == false) {
			mintAddress = reinterpret_cast<uintptr_t>(GetModuleHandleA("Mint.dll"));
			pleioneAddress = reinterpret_cast<uintptr_t>(GetModuleHandleA("Pleione.dll"));
			mabiMessagesR = vector<MabiMessage>();
			mabiMessagesS = vector<MabiMessage>();
			if (DoInjection()) {
				FindMessageFunctions();
				g_mabiMessageHook = true;
				mabiListeners = mabiMods;
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
		log("Patching WriteToNetworkBuffer...");
		result &= PatchWriteToNetworkBuffer();
		log(result ? "...success" : "...failed");

		return result;
	}

	//
	// Hook Types & Vars
	//
	typedef DWORD(__cdecl* mintMessageConstructorSignature)(LPVOID Buffer, LONG Size);
	typedef DWORD(__thiscall* mintMessageDestructorSignature)(LPVOID msgPointer);

	typedef UINT64(__cdecl* mintGetReceiverIdSignature)();
	typedef LPVOID(__cdecl* vmGetInstanceSignature)(UINT64 charId);
	typedef DWORD(__thiscall* mintPostSignature)(LPVOID mintPointer);
	typedef DWORD(__thiscall* pleioneSendSignature)(LPVOID cmessage);
	// Plain 12-byte view of a mint::CMessage, so one can be copied/passed by value.
	struct RawCMessage {
		void* vftable;
		unsigned long state;
		void* payload;
	};
	typedef void*(__thiscall* cmessageCtorSignature)(RawCMessage* self, void* buffer, unsigned long size);
	typedef unsigned long long(__thiscall* cmessageGetReceiverIdSignature)(RawCMessage* self);
	typedef bool(__thiscall* vmSendSignature)(LPVOID vm, UINT64 receiverId, RawCMessage msg);
	
	typedef unsigned long(__thiscall* WriteToNetworkBuffer)(LPVOID cmsg, void* param_1, unsigned long param_2);

	mintMessageConstructorSignature mintMessageConstructor = NULL;
	mintMessageDestructorSignature mintMessageDestructor = NULL;

	mintGetReceiverIdSignature mintGetReceiverId = NULL;
	vmGetInstanceSignature vmGetInstance = NULL;
	mintPostSignature mintPost = NULL;
	pleioneSendSignature pleioneSend = NULL;
	vmSendSignature mintVmSend = NULL;

	LPVOID SavedRecvPointer = NULL;
	LPVOID SavedSendPointer = NULL;
	LONG ReadFromNetworkBufferHookContinueAddress;
	LONG PleioneSendHookContinueAddress;

	// The live network connection object, captured from pleioneSend's `this` (ECX).
	LPVOID g_senderContext = NULL;
	BOOL InsideHookHandler = FALSE;

	//
	// Hook Handlers
	//

	void __stdcall ReadFromNetworkBufferHookHandler(LPVOID MsgPointer, LPVOID Buffer, LONG Size) {
		if (!InsideHookHandler) {
			InsideHookHandler = TRUE;
			SavedRecvPointer = MsgPointer;
			ListenDownstream(Buffer, Size);
			InsideHookHandler = FALSE;
		}
	}

	void __stdcall WriteToNetworkBufferHookHandler(LPVOID Buffer, LONG Size) {
		if (!InsideHookHandler) {
			InsideHookHandler = TRUE;
			ListenUpstream(Buffer, Size);
			InsideHookHandler = FALSE;
		}
	}

	// Runs from CPleione::Run each frame (main thread)
	void __stdcall PleioneSendHookHandler(LPVOID sender) {
		g_senderContext = sender;
	}

	//
	// Hook Traps
	//

	__declspec(naked) void ReadFromNetworkBufferHookTrap() {
		__asm {
			PUSHAD
			mov     ebp, esp

			// Size
			mov     eax, [ebp + 32 + 8]
			push    eax

			// Buffer
			mov     eax, [ebp + 32 + 4]
			push    eax

			// MsgPointer
			push	ecx
			call    ReadFromNetworkBufferHookHandler
			POPAD

			push    ebp
			mov     ebp, esp
			sub     esp, 28
			jmp     ReadFromNetworkBufferHookContinueAddress
		}
	}

	__declspec(naked) void WriteToNetworkBufferHookTrap() {
		__asm {
			PUSHAD
			// Size
			mov     eax, [ebp + 0xC]
			push    eax

			// Buffer
			mov     eax, [ebp + 8]
			push    eax

			call    WriteToNetworkBufferHookHandler
			POPAD

			pop     edi
			pop     esi
			pop     ebx
			leave
			ret     0x8
		}
	}

	// pleioneSend is __thiscall, so the connection object is in ECX on entry.
	__declspec(naked) void PleioneSendHookTrap() {
		__asm {
			PUSHAD
			PUSH ECX
			call    PleioneSendHookHandler
			POPAD

			// Replay the overwritten prologue bytes:  push 0x4 ; mov eax, 0x63D116B3
			push    0x4
			mov     eax, 0x63D116B3

			jmp     PleioneSendHookContinueAddress
		}
	}

	VOID ListenDownstream(LPVOID Buffer, LONG Size) {
		MabiMessage mabiMessage;
		mabiMessage.buffer = (unsigned char*)Buffer;
		mabiMessage.size = Size;

		for (uint32_t i = 0; i < mabiListeners->size(); i++) {
			if ((*mabiListeners)[i]->m_isEnabled && (*mabiListeners)[i]->getHasRecv()) {
				for each(int listenOp in(*mabiListeners)[i]->getOp())
					// Call GetOP every time incase OP has changed
					if (GetOP(mabiMessage.buffer) == listenOp || -1 == listenOp) {
						(*mabiListeners)[i]->onRecv(mabiMessage);
						break;
					}
			}
		}

		InjectRecvQueue();
		InjectSendQueue();
	}

	VOID ListenUpstream(LPVOID Buffer, LONG Size) {
		MabiMessage mabiMessage;
		mabiMessage.buffer = (unsigned char*)Buffer;
		mabiMessage.size = Size;

		for (uint32_t i = 0; i < mabiListeners->size(); i++) {
			if ((*mabiListeners)[i]->m_isEnabled && (*mabiListeners)[i]->getHasSend()) {
				for each(int listenOp in(*mabiListeners)[i]->getOp())
					// Call GetOP every time incase OP has changed
					if (GetOP(mabiMessage.buffer) == listenOp || -1 == listenOp) {
						(*mabiListeners)[i]->onSend(mabiMessage);
						break;
					}
			}
		}

		InjectRecvQueue();
		InjectSendQueue();
	}

	VOID InjectRecvQueue()
	{
		if (mabiMessagesR.size() > 0)
		{
			for each(MabiMessage msg in mabiMessagesR)
			{
				Recv(msg.buffer, msg.size);
				delete[] msg.buffer;
			}
			mabiMessagesR.clear();
		}
	}

	VOID InjectSendQueue()
	{
		if (mabiMessagesS.size() > 0)
		{
			for each(MabiMessage msg in mabiMessagesS)
			{
				Send(msg.buffer, msg.size);
				delete[] msg.buffer;
			}
			mabiMessagesS.clear();
		}
	}

	//
	// Patching
	//

	BOOL MabiMessageHook::PatchReadFromNetworkBuffer() {
		std::optional<uintptr_t> ReadFromNetworkBufferFunctionAddressPtr = kanan::scan("Mint.dll", "55 8B EC 83 EC 1C 53 8B D9");
		DWORD ReadFromNetworkBufferFunctionAddress = *ReadFromNetworkBufferFunctionAddressPtr;
		LONG ReadFromNetworkBufferFunctionAddressLong = *(LONG*)(void*)(&ReadFromNetworkBufferFunctionAddress);
		ReadFromNetworkBufferHookContinueAddress = ReadFromNetworkBufferFunctionAddressLong + 6;

		return Hookjmp((void*)(ReadFromNetworkBufferFunctionAddressLong), ReadFromNetworkBufferHookTrap, 6);
	}

	BOOL MabiMessageHook::PatchWriteToNetworkBuffer() {
		int hookOffset = 385;
		DWORD WriteToNetworkBufferFunctionAddress = (mintAddress + 0x60dc5);
		LONG WriteToNetworkBufferFunctionAddressLong = *(LONG*)(void*)(&WriteToNetworkBufferFunctionAddress);

		// Returning at end of function
		// No need for a continue address

		return Hookjmp((void*)(WriteToNetworkBufferFunctionAddressLong + hookOffset), WriteToNetworkBufferHookTrap,7);
	}

	// Hooks the top of the per-frame CPleione update (0x6381E8AC, RVA 0x1E8AC).
	BOOL MabiMessageHook::PatchRun() {
		// typedef void(__thiscall* pleioneSendSignature)(LPVOID mintPointer);
		DWORD pleioneSendFunctionAddress = (pleioneAddress + 0x4F988);
		LONG pleioneSendFunctionAddressLong = *(LONG*)(void*)(&pleioneSendFunctionAddress);
		pleioneSend = (pleioneSendSignature)pleioneSendFunctionAddressLong;

		// Detour pleioneSend so its `this` (the connection object) is recorded.
		PleioneSendHookContinueAddress = pleioneSendFunctionAddressLong + 7;
		return Hookjmp((void*)(pleioneSendFunctionAddressLong), PleioneSendHookTrap, 7);
	}

	void MabiMessageHook::FindMessageFunctions() {
		// typedef DWORD(__thiscall* mintMessageConstructorSignature)(LPVOID cmsg, LPVOID Buffer, LONG Size);
		DWORD mintMessageConsturctorFunctionAddress = (mintAddress + 0x61666);
		LONG mintMessageConsturctorFunctionAddressLong = *(LONG*)(void*)(&mintMessageConsturctorFunctionAddress);

		mintMessageConstructor = (mintMessageConstructorSignature)mintMessageConsturctorFunctionAddressLong;

		// typedef DWORD(__thiscall* mintMessageDestructorSignature)(LPVOID mintPointer);
		DWORD mintMessageDestructorFunctionAddress = (mintAddress + 0x6170a);
		LONG mintMessageDestructorFunctionAddressLong = *(LONG*)(void*)(&mintMessageDestructorFunctionAddress);

		mintMessageDestructor = (mintMessageDestructorSignature)mintMessageDestructorFunctionAddressLong;
		
		// typedef DWORD(__thiscall* mintGetReceiverIdSignature)(LPVOID mintPointer);
		DWORD mintGetReceiverIdAddress = (mintAddress + 0x60d86);
		LONG mintGetReceiverIdAddressLong = *(LONG*)(void*)(&mintGetReceiverIdAddress);

		mintGetReceiverId = (mintGetReceiverIdSignature)mintGetReceiverIdAddressLong;

		// typedef LPVOID(__thiscall* vmGetInstanceSignature)();
		DWORD vmGetInstanceFunctionAddress = (mintAddress + 0x12be);
		LONG vmGetInstanceFunctionAddressLong = *(LONG*)(void*)(&vmGetInstanceFunctionAddress);

		vmGetInstance = (vmGetInstanceSignature)vmGetInstanceFunctionAddressLong;

		// typedef void(__thiscall* mintPostSignature)(LPVOID mintPointer);
		DWORD mintPostFunctionAddress = (mintAddress + 0x65a7a);
		LONG mintPostFunctionAddressLong = *(LONG*)(void*)(&mintPostFunctionAddress);

		mintPost = (mintPostSignature)mintPostFunctionAddressLong;

		// typedef bool(__thiscall* vmSendSignature)(UINT64 receiverId, LPVOID msg);
		DWORD mintVmSendFunctionAddress = (mintAddress + 0x65c96);
		LONG mintVmSendFunctionAddressLong = *(LONG*)(void*)(&mintVmSendFunctionAddress);

		mintVmSend = (vmSendSignature)mintVmSendFunctionAddressLong;
	}

	void AddToRecvQ(MabiMessage mabiMessage)
	{
		mabiMessagesR.push_back(mabiMessage);
	}

	void AddToSendQ(MabiMessage mabiMessage)
	{
		mabiMessagesS.push_back(mabiMessage);
	}

	int Recv(BYTE* buffer, unsigned int size)
	{
		if (mintMessageConstructor == NULL || mintGetReceiverId == NULL ||
			vmGetInstance == NULL || mintPost == NULL) {
			return 0;
		}

		BOOL wasInside = InsideHookHandler;
		InsideHookHandler = TRUE;

		try {
			RawCMessage msg;
			((cmessageCtorSignature)mintMessageConstructor)(&msg, buffer, size);
			unsigned long long receiverId = ((cmessageGetReceiverIdSignature)mintGetReceiverId)(&msg);
			void* vm = vmGetInstance(receiverId);

			InsideHookHandler = wasInside;
			return ((bool(__thiscall*)(LPVOID, UINT64, RawCMessage))mintPost)(vm, receiverId, msg) ? 1 : 0;
		}
		catch (...) {
			log("kanan: injection threw a C++ exception (ignored).");
			InsideHookHandler = wasInside;
			return 0;
		}
	}

	int Send(BYTE* buffer, unsigned int size)
	{
		if (mintMessageConstructor == NULL || pleioneSend == NULL || g_senderContext == NULL) {
			return 0;
		}

		BOOL wasInside = InsideHookHandler;
		InsideHookHandler = TRUE;

		try {
			RawCMessage msg;
			((cmessageCtorSignature)mintMessageConstructor)(&msg, buffer, size);

			bool ok = ((bool(__thiscall*)(LPVOID, RawCMessage))pleioneSend)(g_senderContext, msg);
			InsideHookHandler = wasInside;
			return ok ? 1 : 0;
		}
		catch (...) {
			InsideHookHandler = wasInside;
			log("kanan: injection threw a C++ exception (ignored).");
			return 0;
		}
	}
}