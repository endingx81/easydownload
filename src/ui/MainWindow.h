#pragma once
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include "../core/DownloadTask.h"

namespace EasyDownload {

class MainWindow {
public:
    static MainWindow& Instance();

    bool Create();
    void Show(int nCmdShow);
    HWND GetHwnd() const { return m_hWnd; }

private:
    MainWindow();
    ~MainWindow();

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static VOID CALLBACK UpdateTimerProc(HWND hWnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void InitControls();
    void Relayout(int width, int height);
    void UpdateDpiMetrics(UINT dpi);
    void RefreshTasksList();
    void UpdateTasksRealtime();
    void UpdateSegmentBar();
    void InitTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayNotification(const std::wstring& title, const std::wstring& message);

    void OnNewDownload();
    void OnNewVideoDownload(const std::wstring& initUrl = L"", const std::wstring& initTitle = L"", const std::wstring& referer = L"", const std::wstring& cookies = L"");
    void OnStartSelected();
    void OnPauseSelected();
    void OnDeleteSelected();
    void OnOpenSelectedFolder();
    void OnOpenSelectedFile();
    void OnOpenSelectedBrowser();
    void OnCopySelectedUrl();
    void OnShowSelectedInfo();
    void OnSettings();
    void OnInstallExtension();
    void OnToggleDpiBypass();
    void UpdateDpiBypassUi();
    void OnToggleChromeDoh();

    HWND m_hWnd{ NULL };
    HWND m_hToolbar{ NULL };
    HWND m_hListView{ NULL };
    HWND m_hSegmentBar{ NULL };
    HWND m_hStatusBar{ NULL };

    HFONT m_hFont{ NULL };
    HFONT m_hBoldFont{ NULL };

    UINT m_dpi{ 96 };
    NOTIFYICONDATAW m_nid{};
    bool m_trayActive{ false };
    bool m_isDraggingOrSizing{ false };

    std::wstring m_selectedTaskId;
};

} // namespace EasyDownload
