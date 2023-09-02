#include <windows.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>

int REPEAT_START_DELAY = 200;
int REPEAT_INTERVAL = 20;

// Flags to control the keypress threads
std::atomic<bool> sendLeft(false);
std::atomic<bool> sendRight(false);

std::chrono::steady_clock::time_point buttonPressTimeLeft;
std::atomic<bool> firstPressRight(true);

std::chrono::steady_clock::time_point buttonPressTimeRight;
std::atomic<bool> firstPressLeft(true);

std::condition_variable cvRight;
std::mutex cv_m_Right;

std::condition_variable cvLeft;
std::mutex cv_m_Left;


void sendKeyPress(UINT key)
{
	INPUT ip = { 0 };
	ip.type = INPUT_KEYBOARD;
	ip.ki.wVk = key;
	ip.ki.dwFlags = 0;
	SendInput(1, &ip, sizeof(INPUT));
}

void sendKeyRelease(UINT key)
{
	INPUT ip = { 0 };
	ip.type = INPUT_KEYBOARD;
	ip.ki.wVk = key;
	ip.ki.dwFlags = KEYEVENTF_KEYUP;
	SendInput(1, &ip, sizeof(INPUT));
}


void sendLeftThreadFunc()
{
	std::unique_lock<std::mutex> lk(cv_m_Left);
	while (true)
	{
		if (sendLeft)
		{
			if (firstPressLeft) {
				sendKeyPress(VK_LEFT);
				firstPressLeft = false;
				buttonPressTimeLeft = std::chrono::steady_clock::now();
			}
			else if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - buttonPressTimeLeft).count() >= REPEAT_START_DELAY) {
				sendKeyPress(VK_LEFT);
				cvLeft.wait_for(lk, std::chrono::milliseconds(REPEAT_INTERVAL));
			}
			else {
				cvLeft.wait_for(lk, std::chrono::milliseconds(REPEAT_START_DELAY));
			}
		}
		else
		{
			cvLeft.wait(lk);
		}
	}
}


void sendRightThreadFunc()
{
	std::unique_lock<std::mutex> lk(cv_m_Right);
	while (true)
	{
		if (sendRight)
		{
			if (firstPressRight) {
				sendKeyPress(VK_RIGHT);
				firstPressRight = false;
				buttonPressTimeRight = std::chrono::steady_clock::now();
			}
			else if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - buttonPressTimeRight).count() >= REPEAT_START_DELAY) {
				sendKeyPress(VK_RIGHT);
				cvRight.wait_for(lk, std::chrono::milliseconds(REPEAT_INTERVAL));
			}
			else {
				cvRight.wait_for(lk, std::chrono::milliseconds(REPEAT_START_DELAY));
			}
		}
		else
		{
			cvRight.wait(lk);
		}
	}
}

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
				buttonPressTimeRight = std::chrono::steady_clock::now();
				sendRight = true;
				firstPressRight = true;
				cvRight.notify_all();
				return 1;
			}
			else if (xButton == XBUTTON2)
			{
				buttonPressTimeLeft = std::chrono::steady_clock::now();
				sendLeft = true;
				firstPressLeft = true;
				cvLeft.notify_all();
				return 1;
			}
		}
		else if (wParam == WM_XBUTTONUP)
		{
			if (xButton == XBUTTON1)
			{
				firstPressRight = true;
				sendRight = false;
				sendKeyRelease(VK_RIGHT);
				return 1;
			}
			else if (xButton == XBUTTON2)
			{
				sendLeft = false;
				firstPressLeft = true;
				sendKeyRelease(VK_LEFT);
				return 1;
			}
		}
	}

	return CallNextHookEx(NULL, nCode, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	std::thread sendLeftThread(sendLeftThreadFunc);
	std::thread sendRightThread(sendRightThreadFunc);

	HHOOK mouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, hInstance, 0);
	AllocConsole();

	FILE* stream;
	freopen_s(&stream, "CONOUT$", "w", stdout);

	int argc;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argc > 2) {
		int parsedStartDelay = _wtoi(argv[1]);
		int parsedInterval = _wtoi(argv[2]);

		if (parsedStartDelay != 0) {
			REPEAT_START_DELAY = parsedStartDelay;
		}
		else {
			printf("Warning: Could not parse REPEAT_START_DELAY argument. Using default value.\n");
		}

		if (parsedInterval != 0) {
			REPEAT_INTERVAL = parsedInterval;
		}
		else {
			printf("Warning: Could not parse REPEAT_INTERVAL argument. Using default value.\n");
		}
	}
	LocalFree(argv);


	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	UnhookWindowsHookEx(mouseHook);

	sendLeftThread.join();
	sendRightThread.join();

	return 0;
}