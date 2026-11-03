#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <shlwapi.h>
#include "core/Settings.h"
#include "core/DownloadManager.h"
#include "core/LocalServer.h"
#include "ui/MainWindow.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    // 1. Single Instance Check
    HANDLE hMutex = CreateMutexW(NULL, FALSE, L"Local\\EasyDownload_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExisting = FindWindowW(L"EasyDownloadMainClass", NULL);
        if (hExisting) {
            ShowWindow(hExisting, SW_RESTORE);
            SetForegroundWindow(hExisting);
        }
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    // 2. High-DPI Per-Monitor V2 setup (Fallback if manifest isn't caught)
    typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
    auto pSetContext = (PFN_SetProcessDpiAwarenessContext)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
    if (pSetContext) {
        pSetContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    // 3. COM & Common Controls
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX) };
    icex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS | ICC_UPDOWN_CLASS | ICC_COOL_CLASSES;
    InitCommonControlsEx(&icex);

    // 4. Initialize Core Modules
    EasyDownload::Settings::Instance().Load();
    EasyDownload::DownloadManager::Instance().Initialize();

    int port = EasyDownload::Settings::Instance().GetBridgePort();
    EasyDownload::LocalServer::Instance().Start(port);

    // 5. Create UI
    auto& mainWin = EasyDownload::MainWindow::Instance();
    if (!mainWin.Create()) {
        MessageBoxW(NULL, L"EasyDownload 메인 창을 생성할 수 없습니다.", L"오류", MB_OK | MB_ICONERROR);
        return 1;
    }

    // 6. Handle CLI arguments
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(lpCmdLine, &argc);
    bool startMinimized = EasyDownload::Settings::Instance().GetStartMinimized();

    if (argv && argc > 0) {
        for (int i = 0; i < argc; ++i) {
            if (_wcsicmp(argv[i], L"--minimized") == 0) {
                startMinimized = true;
            } else if (wcsstr(argv[i], L"http://") || wcsstr(argv[i], L"https://")) {
                EasyDownload::DownloadManager::Instance().AddTask(argv[i]);
            }
        }
        LocalFree(argv);
    }

    mainWin.Show(startMinimized ? SW_HIDE : nCmdShow);

    // 7. Message Loop
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 8. Cleanup
    EasyDownload::LocalServer::Instance().Stop();
    EasyDownload::DownloadManager::Instance().Shutdown();
    CoUninitialize();

    if (hMutex) {
        CloseHandle(hMutex);
    }
    return (int)msg.wParam;
}
