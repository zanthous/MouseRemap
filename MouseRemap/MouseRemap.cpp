#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <vector>

// Configuration
int REPEAT_START_DELAY = 200; // Milliseconds before repeat starts
int REPEAT_INTERVAL = 20;    // Milliseconds between repeats

// --- Thread Control Flags ---
std::atomic<bool> sendLeft(false);
std::atomic<bool> sendRight(false);

// --- State Flags for Shift Simulation ---
// These track if WE sent a simulated Shift down that needs releasing
std::atomic<bool> simulatedShiftDownLeft(false);
std::atomic<bool> simulatedShiftDownRight(false);


// --- Condition Variables for Thread Synchronization ---
std::condition_variable cvRight;
std::mutex cv_m_Right;

std::condition_variable cvLeft;
std::mutex cv_m_Left;

std::atomic<bool> sendInputFailureLogged(false);

bool isProcessElevated()
{
    HANDLE token = NULL;
    if(!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        return false;
    }

    TOKEN_ELEVATION elevation = {};
    DWORD size = 0;
    BOOL success = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);

    return success && elevation.TokenIsElevated;
}

bool relaunchElevated(LPCSTR arguments)
{
    char exePath[MAX_PATH];
    if(GetModuleFileNameA(NULL, exePath, MAX_PATH) == 0)
    {
        return false;
    }

    SHELLEXECUTEINFOA executeInfo = {};
    executeInfo.cbSize = sizeof(executeInfo);
    executeInfo.lpVerb = "runas";
    executeInfo.lpFile = exePath;
    executeInfo.lpParameters = arguments;
    executeInfo.nShow = SW_SHOWNORMAL;

    return ShellExecuteExA(&executeInfo);
}

void sendInputChecked(INPUT& input, const char* description)
{
    if(SendInput(1, &input, sizeof(INPUT)) != 1 && !sendInputFailureLogged.exchange(true))
    {
        std::cerr << "Warning: SendInput failed while sending " << description
                  << ". Error Code: " << GetLastError()
                  << ". If this happens only in elevated Windows apps, run MouseRemap elevated." << std::endl;
    }
}

// --- Input Simulation Functions ---

/**
 * @brief Sends a key down event for the specified key ONLY.
 *        Does NOT handle Shift internally anymore.
 * @param key The virtual key code (e.g., VK_LEFT, VK_RIGHT) to press.
 */
void sendKeyPress(UINT key)
{
    INPUT ip = {0};
    ip.type = INPUT_KEYBOARD;

    if(key == VK_LEFT)
    {
        ip.ki.wVk = 0;
        ip.ki.wScan = 0x4B; // E0 4B (extended left arrow)
        ip.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_EXTENDEDKEY;
    }
    else if(key == VK_RIGHT)
    {
        ip.ki.wVk = 0;
        ip.ki.wScan = 0x4D; // E0 4D (extended right arrow)
        ip.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_EXTENDEDKEY;
    }
    else
    {
        ip.ki.wVk = key;
        ip.ki.dwFlags = 0; // Key down
    }

    sendInputChecked(ip, "key down");
}

/**
 * @brief Sends a key up event for the specified key ONLY.
 *        Does NOT handle Shift internally anymore.
 * @param key The virtual key code (e.g., VK_LEFT, VK_RIGHT) to release.
 */
void sendKeyRelease(UINT key)
{
    INPUT ip = {0};
    ip.type = INPUT_KEYBOARD;

    if(key == VK_LEFT)
    {
        ip.ki.wVk = 0;
        ip.ki.wScan = 0x4B; // E0 4B (extended left arrow)
        ip.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP;
    }
    else if(key == VK_RIGHT)
    {
        ip.ki.wVk = 0;
        ip.ki.wScan = 0x4D; // E0 4D (extended right arrow)
        ip.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP;
    }
    else
    {
        ip.ki.wVk = key;
        ip.ki.dwFlags = KEYEVENTF_KEYUP; // Key up
    }

    sendInputChecked(ip, "key up");
}

/**
 * @brief Sends a key event (down or up) for VK_SHIFT.
 * @param press True to send key down, false to send key up.
 */
void sendShiftEvent(bool press)
{
    INPUT ip = {0};
    ip.type = INPUT_KEYBOARD;
    ip.ki.wVk = VK_SHIFT; // Generic Shift
    // Consider using VK_LSHIFT or VK_RSHIFT if generic causes issues
    // UINT specificShift = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) ? VK_LSHIFT : VK_RSHIFT;
    // ip.ki.wVk = specificShift;
    ip.ki.dwFlags = press ? 0 : KEYEVENTF_KEYUP;
    sendInputChecked(ip, press ? "shift down" : "shift up");
}


// --- Key Repeat Threads ---

void sendLeftThreadFunc()
{
    bool firstPress = true; // Local state for first press logic within an activation
    bool shift_was_active_at_start = false; // Track if Shift was held when sequence began

    std::unique_lock<std::mutex> lk(cv_m_Left);
    while(true)
    {
        // Wait until sendLeft is true
        cvLeft.wait(lk, [] { return sendLeft.load(); });

        // --- Start of activation sequence ---
        firstPress = true;
        shift_was_active_at_start = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) || (GetAsyncKeyState(VK_RSHIFT) & 0x8000);
        simulatedShiftDownLeft = false; // Reset our tracking flag

        // Send initial Shift Down IF physical Shift is held
        if(shift_was_active_at_start)
        {
            sendShiftEvent(true); // Send VK_SHIFT DOWN
            simulatedShiftDownLeft = true; // Mark that we sent it
            // Optional short delay after sending Shift, before first arrow key
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        // Keep sending while sendLeft remains true
        while(sendLeft)
        {
            sendKeyPress(VK_LEFT); // Send only the arrow key

            if(firstPress)
            {
                firstPress = false; // Mark initial press done
                // Wait for the initial repeat delay or until sendLeft becomes false
                cvLeft.wait_for(lk, std::chrono::milliseconds(REPEAT_START_DELAY), [] { return !sendLeft.load(); });
            }
            else
            {
                // Wait for the repeat interval or until sendLeft becomes false
                cvLeft.wait_for(lk, std::chrono::milliseconds(REPEAT_INTERVAL), [] { return !sendLeft.load(); });
            }
            // Check flag again after wait, break if it became false during sleep
            if(!sendLeft.load()) break;
        }

        // --- End of activation sequence ---
        // When sendLeft becomes false (loop broken or wait predicate failed):

        // 1. Release the Arrow Key
        sendKeyRelease(VK_LEFT);

        // 2. Release Shift IF WE simulated its press
        if(simulatedShiftDownLeft.load())
        {
            // Optional delay before releasing Shift
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            sendShiftEvent(false); // Send VK_SHIFT UP
            simulatedShiftDownLeft = false; // Clear our tracking flag
        }
        // Reset firstPress state variable conceptually (done at start of next activation)
    }
}

void sendRightThreadFunc()
{
    bool firstPress = true; // Local state for first press logic within an activation
    bool shift_was_active_at_start = false; // Track if Shift was held when sequence began

    std::unique_lock<std::mutex> lk(cv_m_Right);
    while(true)
    {
        // Wait until sendRight is true
        cvRight.wait(lk, [] { return sendRight.load(); });

        // --- Start of activation sequence ---
        firstPress = true;
        shift_was_active_at_start = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) || (GetAsyncKeyState(VK_RSHIFT) & 0x8000);
        simulatedShiftDownRight = false; // Reset our tracking flag

        // Send initial Shift Down IF physical Shift is held
        if(shift_was_active_at_start)
        {
            sendShiftEvent(true); // Send VK_SHIFT DOWN
            simulatedShiftDownRight = true; // Mark that we sent it
            // Optional short delay
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        // Keep sending while sendRight remains true
        while(sendRight)
        {
            sendKeyPress(VK_RIGHT); // Send only the arrow key

            if(firstPress)
            {
                firstPress = false; // Mark initial press done
                // Wait for the initial repeat delay or until sendRight becomes false
                cvRight.wait_for(lk, std::chrono::milliseconds(REPEAT_START_DELAY), [] { return !sendRight.load(); });
            }
            else
            {
                // Wait for the repeat interval or until sendRight becomes false
                cvRight.wait_for(lk, std::chrono::milliseconds(REPEAT_INTERVAL), [] { return !sendRight.load(); });
            }
            // Check flag again after wait
            if(!sendRight.load()) break;
        }

        // --- End of activation sequence ---
        // When sendRight becomes false

        // 1. Release the Arrow Key
        sendKeyRelease(VK_RIGHT);

        // 2. Release Shift IF WE simulated its press
        if(simulatedShiftDownRight.load())
        {
            // Optional delay
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            sendShiftEvent(false); // Send VK_SHIFT UP
            simulatedShiftDownRight = false; // Clear our tracking flag
        }
    }
}


// --- Low-Level Mouse Hook Procedure ---

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if(nCode == HC_ACTION)
    {
        MSLLHOOKSTRUCT* pMouseStruct = (MSLLHOOKSTRUCT*)lParam;
        WORD xButton = GET_XBUTTON_WPARAM(pMouseStruct->mouseData);

        if(wParam == WM_XBUTTONDOWN)
        {
            if(xButton == XBUTTON1) // Typically "Forward" button
            {
                if(!sendRight.load())
                {
                    sendRight = true;       // Set flag FIRST
                    cvRight.notify_one();   // THEN wake up right thread
                }
                return 1; // Block further processing
            }
            else if(xButton == XBUTTON2) // Typically "Back" button
            {
                if(!sendLeft.load())
                {
                    sendLeft = true;      // Set flag FIRST
                    cvLeft.notify_one();  // THEN wake up left thread
                }
                return 1; // Block further processing
            }
        }
        else if(wParam == WM_XBUTTONUP)
        {
            if(xButton == XBUTTON1)
            {
                if(sendRight.load())
                {
                    sendRight = false;    // Clear flag
                    // No need to notify here IF the thread's wait predicate checks the flag.
                    // It will wake up, see the flag is false, and exit the inner loop.
                    // However, notifying doesn't hurt and ensures faster exit if waiting.
                    cvRight.notify_one();
                }
                return 1; // Block further processing
            }
            else if(xButton == XBUTTON2)
            {
                if(sendLeft.load())
                {
                    sendLeft = false;     // Clear flag
                    cvLeft.notify_one();  // Notify for potentially faster exit
                }
                return 1; // Block further processing
            }
        }
        // *** Potential Issue Point ***
        // If another mouse message (like WM_MOUSEMOVE) happens *between*
        // XBUTTONDOWN and XBUTTONUP while Shift is physically held,
        // does that somehow interfere? Unlikely but possible.
        // Let's keep blocking only XButton events for now.
    }

    // Pass non-handled messages along the hook chain
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

// --- Main Application Entry Point ---
// (WinMain remains the same as the previous version - no changes needed there)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    if(!isProcessElevated())
    {
        if(relaunchElevated(lpCmdLine))
        {
            return 0;
        }
    }

    // Optional: Create a console for debugging output
    AllocConsole();
    FILE* stream;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    std::cout << "Starting Mouse XButton to Arrow Key Repeater (V3 - Single Shift Logic)..." << std::endl;

    if(!isProcessElevated())
    {
        std::cout << "Warning: MouseRemap is not elevated. Windows blocks lower-integrity apps from "
                  << "reliably injecting input into elevated apps such as Task Manager." << std::endl;
    }

    // --- Parse Command Line Arguments for Delays ---
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if(argc > 2)
    {
        int parsedStartDelay = _wtoi(argv[1]);
        int parsedInterval = _wtoi(argv[2]);

        if(parsedStartDelay > 0) // Basic validation
        {
            REPEAT_START_DELAY = parsedStartDelay;
        }
        else if(argc > 1)
        {
            std::cout << "Warning: Invalid REPEAT_START_DELAY argument. Using default: " << REPEAT_START_DELAY << " ms" << std::endl;
        }

        if(parsedInterval > 0) // Basic validation
        {
            REPEAT_INTERVAL = parsedInterval;
        }
        else if(argc > 2)
        {
            std::cout << "Warning: Invalid REPEAT_INTERVAL argument. Using default: " << REPEAT_INTERVAL << " ms" << std::endl;
        }
        std::cout << "Using Delays - Start: " << REPEAT_START_DELAY << " ms, Interval: " << REPEAT_INTERVAL << " ms" << std::endl;

    }
    else
    {
        std::cout << "Using default delays. Start Delay: " << REPEAT_START_DELAY << " ms, Repeat Interval: " << REPEAT_INTERVAL << " ms" << std::endl;
        std::cout << "Usage: " << "program.exe [start_delay_ms] [repeat_interval_ms]" << std::endl;
    }
    LocalFree(argv); // Free memory allocated by CommandLineToArgvW


    // --- Start Worker Threads ---
    std::thread sendLeftThread(sendLeftThreadFunc);
    std::thread sendRightThread(sendRightThreadFunc);

    // --- Set Up Mouse Hook ---
    HHOOK mouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, hInstance, 0);
    if(!mouseHook)
    {
        std::cerr << "Error: Failed to set mouse hook! Error Code: " << GetLastError() << std::endl;
        if(stream) fclose(stream);
        FreeConsole();
        return 1; // Exit if hook fails
    }
    std::cout << "Mouse hook installed successfully." << std::endl;
    std::cout << "Hold Shift + XButton1/XButton2 for Shift+Right/Shift+Left." << std::endl;

    // --- Message Loop (Keeps the application running) ---
    MSG msg;
    while(GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    std::cout << "Exiting application. Cleaning up..." << std::endl;

    // --- Clean Up ---
    UnhookWindowsHookEx(mouseHook);

    // Signal threads to stop cleanly
    sendLeft = false;
    sendRight = false;
    cvLeft.notify_all();
    cvRight.notify_all();

    // Wait for threads to finish
    if(sendLeftThread.joinable()) sendLeftThread.join();
    if(sendRightThread.joinable()) sendRightThread.join();

    // Clean up console
    if(stream) fclose(stream);
    FreeConsole();

    return (int)msg.wParam;
}
