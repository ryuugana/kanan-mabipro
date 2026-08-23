
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
	std::vector<MabiMessage> mabiMessages;
	unsigned int mintAddress = NULL;

	VOID ListenDownstream(LPVOID Buffer, LONG Size);
	VOID ListenUpstream(LPVOID Buffer, LONG Size);
	VOID InjectRecvQueue();
	extern "C" int Recv(BYTE * buffer, unsigned int size);



	MabiMessageHook::MabiMessageHook(std::vector<std::unique_ptr<MessageMod>>* mabiMods)
	{
		if (g_mabiMessageHook == false) {
			mintAddress = reinterpret_cast<uintptr_t>(GetModuleHandleA("Mint.dll"));
			mabiMessages = vector<MabiMessage>();
			if (DoInjection()) {
				FindMintFunctions();
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
	
	typedef unsigned long(__thiscall* WriteToNetworkBuffer)(LPVOID cmsg, void* param_1, unsigned long param_2);

	mintMessageConstructorSignature mintMessageConstructor = NULL;
	mintMessageDestructorSignature mintMessageDestructor = NULL;

	mintGetReceiverIdSignature mintGetReceiverId = NULL;
	vmGetInstanceSignature vmGetInstance = NULL;
	mintPostSignature mintPost = NULL;

	LPVOID SavedRecvPointer = NULL;
	LPVOID SavedSendPointer = NULL;
	LONG ReadFromNetworkBufferHookContinueAddress;
	LONG RunHookContinueAddress;
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

	void __stdcall RunHookHandler() {
		if (!InsideHookHandler) {
			InsideHookHandler = TRUE;
			InjectRecvQueue();
			InsideHookHandler = FALSE;
		}
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


	__declspec(naked) void RunHookTrap() {
		__asm {
			PUSHAD
			call    RunHookHandler
			POPAD

			push esi
			mov esi,ecx
			mov ecx, [esi+04]

			jmp     RunHookContinueAddress
		}
	}

	VOID ListenDownstream(LPVOID Buffer, LONG Size) {
		MabiMessage mabiMessage;
		mabiMessage.buffer = (unsigned char*)Buffer;
		mabiMessage.size = Size;

		unsigned long op = GetOP(mabiMessage.buffer);
		for (uint32_t i = 0; i < mabiListeners->size(); i++) {
			if ((*mabiListeners)[i]->m_isEnabled && (*mabiListeners)[i]->getHasRecv()) {
				for each(int listenOp in(*mabiListeners)[i]->getOp())
					if (op == listenOp || -1 == listenOp) {
						(*mabiListeners)[i]->onRecv(mabiMessage);
						break;
					}
			}
		}
		InjectRecvQueue();
	}

	VOID ListenUpstream(LPVOID Buffer, LONG Size) {
		MabiMessage mabiMessage;
		mabiMessage.buffer = (unsigned char*)Buffer;
		mabiMessage.size = Size;

		unsigned long op = GetOP(mabiMessage.buffer);
		for (uint32_t i = 0; i < mabiListeners->size(); i++) {
			if ((*mabiListeners)[i]->m_isEnabled && (*mabiListeners)[i]->getHasSend()) {
				for each(int listenOp in(*mabiListeners)[i]->getOp())
					if (op == listenOp || -1 == listenOp) {
						(*mabiListeners)[i]->onSend(mabiMessage);
						break;
					}
			}
		}
		InjectRecvQueue();
	}

	VOID InjectRecvQueue()
	{
		if (mabiMessages.size() > 0)
		{
			for each(MabiMessage msg in mabiMessages)
			{
				Recv(msg.buffer, msg.size);
				free(msg.buffer);
			}
			mabiMessages.clear();
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

	BOOL MabiMessageHook::PatchRun() {
		DWORD RunFunctionAddress = (mintAddress + 0x66a9b);
		LONG RunFunctionAddressLong = *(LONG*)(void*)(&RunFunctionAddress);
		RunHookContinueAddress = RunFunctionAddressLong + 6;

		return Hookjmp((void*)(RunFunctionAddressLong), RunHookTrap, 6);
	}

	void MabiMessageHook::FindMintFunctions() {
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

		// typedef void(__fastcall* mintPostSignature)(LPVOID mintPointer, LONG Unknown, LPVOID MsgMember1, LPVOID MsgMember2, LPVOID MsgMember3);
		DWORD mintPostFunctionAddress = (mintAddress + 0x65a7a);
		LONG mintPostFunctionAddressLong = *(LONG*)(void*)(&mintPostFunctionAddress);

		mintPost = (mintPostSignature)mintPostFunctionAddressLong;
		
	}

	void AddToRecvQ(MabiMessage mabiMessage)
	{
		mabiMessages.push_back(mabiMessage);
	}

	__declspec(naked) int Recv(BYTE* buffer, unsigned int size)
	{
		__asm
		{
			PUSHAD
			SUB        ESP, 0xc
			MOV        ECX, ESP
			MOV        EAX, dword ptr[ESP + 0x34]
			OR         EAX, 0x80000000

			PUSH       EAX
			PUSH       dword ptr[ESP + 0x34]
			CALL       mintMessageConstructor

			MOV        ECX, ESP
			CALL       mintGetReceiverId

			PUSH       EDX
			PUSH       EAX
			CALL       vmGetInstance

			MOV        ECX, EAX
			CALL       mintPost

			MOV        dword ptr[ESP + 0x1c], EAX
			POPAD
			RET        0x8
		}
	}
}