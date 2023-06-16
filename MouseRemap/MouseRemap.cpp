#include <windows.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>

// Flags to control the keypress threads
std::atomic<bool> sendLeft(false);
std::atomic<bool> sendRight(false);

std::chrono::steady_clock::time_point buttonPressTime1;
std::atomic<bool> firstPress1(true);

std::chrono::steady_clock::time_point buttonPressTime2;
std::atomic<bool> firstPress2(true);

std::condition_variable cvRight;
std::mutex cv_m_Right;

std::condition_variable cvLeft;
std::mutex cv_m_Left;

// Thread function for sending left arrow keypresses
void sendLeftThreadFunc()
{
    while (true)
    {
        std::unique_lock<std::mutex> lk(cv_m_Left);
        while (true)
        {
            if (sendLeft)
            {
                if (firstPress1) {
                    keybd_event(VK_LEFT, 0, 0, 0);
                    keybd_event(VK_LEFT, 0, KEYEVENTF_KEYUP, 0);
                    firstPress1 = false;
                    Sleep(500);
                }
                else if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - buttonPressTime1).count() >= 100) {
                    keybd_event(VK_LEFT, 0, 0, 0);
                    keybd_event(VK_LEFT, 0, KEYEVENTF_KEYUP, 0);
                    Sleep(10);
                }
            }
            else
            {
                cvLeft.wait(lk);
            }
        }
    }
}


// Thread function for sending right arrow keypresses
void sendRightThreadFunc()
{
    std::unique_lock<std::mutex> lk(cv_m_Right);
    while (true)
    {
        if (sendRight)
        {
            if (firstPress2) {
                keybd_event(VK_RIGHT, 0, 0, 0);
                keybd_event(VK_RIGHT, 0, KEYEVENTF_KEYUP, 0);
                firstPress2 = false;
                Sleep(100);
            }
            else if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - buttonPressTime2).count() >= 100) {
                keybd_event(VK_RIGHT, 0, 0, 0);
                keybd_event(VK_RIGHT, 0, KEYEVENTF_KEYUP, 0);
                Sleep(10);
            }
        }
        else
        {
            cvRight.wait(lk);
        }
    }
}

// Your mouse hook procedure
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION)
    {
        MSLLHOOKSTRUCT* pMouseStruct = (MSLLHOOKSTRUCT*)lParam;

        WORD xButton = GET_XBUTTON_WPARAM(pMouseStruct->mouseData);

        if (wParam == WM_XBUTTONDOWN)
        {
            if (xButton == XBUTTON1)
            {
                buttonPressTime1 = std::chrono::steady_clock::now();
                sendRight = true;
                firstPress1 = true;
                cvRight.notify_all();
                return 1;
            }
            else if (xButton == XBUTTON2)
            {
                buttonPressTime2 = std::chrono::steady_clock::now();
                sendLeft = true;
                firstPress2 = true;
                cvLeft.notify_all();
                return 1;
            }
        }
        else if (wParam == WM_XBUTTONUP)
        {
            if (xButton == XBUTTON1)
            {
                sendRight = false;
                return 1;
            }
            else if (xButton == XBUTTON2)
            {
                sendLeft = false;
                return 1;
            }
        }
    }

    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Create the keypress threads
    std::thread sendLeftThread(sendLeftThreadFunc);
    std::thread sendRightThread(sendRightThreadFunc);

    HHOOK mouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, hInstance, 0);
    AllocConsole();

    FILE* stream;
    freopen_s(&stream, "CONOUT$", "w", stdout);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(mouseHook);

    // When your program is about to exit, join the threads
    sendLeftThread.join();
    sendRightThread.join();

    return 0;
}