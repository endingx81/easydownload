#pragma once
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include "../core/DownloadTask.h"
#include "../core/LinkExtractor.h"

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
    void OnCopySelectedUrl();
    void OnShowSelectedInfo();
    void OnSettings();
    void OnInstallExtension();
    void OnToggleDpiBypass();
    void UpdateDpiBypassUi();
    void OnToggleChromeDoh();

    // Batch Extractor Handlers
    void InitBatchControls();
    void SwitchTab(int tabIndex);
    void RelayoutBatch(int width, int topY, int bottomY);
    void OnBatchAnalyze();
    void OnBatchAdd(bool onlySelected);
    void OnBatchClear();
    void UpdateBatchListItem(int index);

    HWND m_hWnd{ NULL };
    HWND m_hToolbar{ NULL };
    HWND m_hTabCtrl{ NULL };
    HWND m_hListView{ NULL };
    HWND m_hSegmentBar{ NULL };
    HWND m_hStatusBar{ NULL };

    // Batch Controls
    HWND m_hBatchGuideLbl{ NULL };
    HWND m_hBatchTextEdit{ NULL };
    HWND m_hBatchHostLbl{ NULL };
    HWND m_hBatchHostCombo{ NULL };
    HWND m_hBatchSeqCheck{ NULL };
    HWND m_hBatchBtnAnalyze{ NULL };
    HWND m_hBatchListView{ NULL };
    HWND m_hBatchBtnAddSelected{ NULL };
    HWND m_hBatchBtnAddAll{ NULL };
    HWND m_hBatchBtnClear{ NULL };
    HWND m_hBatchStatusLbl{ NULL };

    HFONT m_hFont{ NULL };
    HFONT m_hBoldFont{ NULL };

    int m_currentTab{ 0 }; // 0: Downloads, 1: Batch Extractor
    std::vector<ExtractedLink> m_batchLinks;
    std::atomic<bool> m_batchAbortFlag{ false };
    bool m_batchAnalyzing{ false };

    UINT m_dpi{ 96 };
    NOTIFYICONDATAW m_nid{};
    bool m_trayActive{ false };

    std::wstring m_selectedTaskId;
};

} // namespace EasyDownload
