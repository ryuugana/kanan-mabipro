#include "WinProcServer.hpp"

#include "MessageMod.hpp"
#include "Log.hpp"

namespace kanan {
        LRESULT CALLBACK WinProcServer::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
            WinProcServer* pThis = nullptr;

            if (uMsg == WM_NCCREATE) {
                CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
                pThis = reinterpret_cast<WinProcServer*>(pCreate->lpCreateParams);
                SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
            }
            else {
                pThis = reinterpret_cast<WinProcServer*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
            }

            if (pThis) {
                return pThis->HandleMessage(hwnd, uMsg, wParam, lParam);
            }

            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }

        LRESULT WinProcServer::HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
            switch (uMsg) {
            case WM_COPYDATA: {
                HWND clientHWnd = reinterpret_cast<HWND>(wParam);
                COPYDATASTRUCT* cds = reinterpret_cast<COPYDATASTRUCT*>(lParam);

                int command = static_cast<int>(cds->dwData);

                if (command == Sign::Connect) {
                    // Handle Connect
                    m_clientHWnd = clientHWnd;
                    m_isConnected = true;
                    log("WinProc connected");
                }
                else if (command == Sign::Disconnect) {
                    // Handle Disconnect
                    if (m_clientHWnd == clientHWnd) {
                        m_clientHWnd = nullptr;
                        m_isConnected = false;
                        log("WinProc disconnected");
                    }
                }
                else if (command == Sign::Recv && m_isConnected) {
                    // Handle incoming packet data sent from the client to the server
                    if (cds->cbData > 0 && cds->lpData != nullptr) {
                        // Allocate a copy of the buffer so it persists outside the WM_COPYDATA scope
                        MabiMessage msg;
                        msg.buffer = new unsigned char[cds->cbData];
                        std::memcpy(msg.buffer, cds->lpData, cds->cbData);

                        msg.size = static_cast<LONG>(cds->cbData);

                        AddToRecvQ(msg);
                    }
                }
                return TRUE;
            }
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            }
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }

        void WinProcServer::msgLoop()
        {
            MSG msg = { 0 };
            while (GetMessage(&msg, nullptr, 0, 0)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        bool WinProcServer::Initialize(HINSTANCE hInstance) {
            if (m_isInitialized) return m_isInitialized;

            WCHAR* className = L"mod_Alissa";
            WNDCLASSEX wc = { 0 };
            wc.cbSize = sizeof(WNDCLASSEX);
            wc.lpfnWndProc = WinProcServer::WindowProc;
            wc.hInstance = hInstance;
            wc.lpszClassName = className;

            if (!RegisterClassEx(&wc)) {
                return false;
            }

            m_hWnd = CreateWindowEx(
                0, className, className,
                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, hInstance, this
            );

            m_isInitialized = (m_hWnd != nullptr);

            return m_isInitialized;
        }

        // Send a packet or message back to the connected C# client
        void WinProcServer::SendToClient(int opCode, const BYTE* data, size_t size) {
            if (!m_isInitialized || !m_isConnected || !m_clientHWnd) {
                return;
            }

            COPYDATASTRUCT cds;
            cds.dwData = opCode; // e.g., Sign::Recv
            cds.cbData = size;
            cds.lpData = const_cast<BYTE*>(data);

            // Send message back to the client's window handle
            SendMessage(m_clientHWnd, WM_COPYDATA, reinterpret_cast<WPARAM>(m_hWnd), reinterpret_cast<LPARAM>(&cds));
        }

        void WinProcServer::DisconnectClient() {
            m_isConnected = false;
            m_clientHWnd = nullptr;
        }
}