#include <windows.h>
#include <vector>
#include <thread>

namespace kanan {
    namespace Sign {
        constexpr int Connect = 100;
        constexpr int Disconnect = 101;
        constexpr int Recv = 0x10101012;
        constexpr int Send = 0x10101011;
    }

    class WinProcServer {
    private:
        HWND m_hWnd = nullptr;
        HWND m_clientHWnd = nullptr;
        bool m_isConnected = false;
        bool m_isInitialized = false;
        std::thread msgThread;

        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
        LRESULT HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
        void msgLoop();

    public:
        bool Initialize(HINSTANCE hInstance);
        bool isInitialized() { return m_isInitialized; };
        void SendToClient(int opCode, const BYTE* data, size_t size);
        void DisconnectClient();
    };
}