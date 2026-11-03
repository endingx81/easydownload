#include "MainWindow.h"
#include "SegmentBar.h"
#include "NewDownloadDialog.h"
#include "VideoDownloadDialog.h"
#include "SettingsDialog.h"
#include "DownloadProgressDialog.h"
#include "DownloadInfoDialog.h"
#include "FormatUtils.h"
#include "../resource.h"
#include "../core/DownloadManager.h"
#include "../core/HttpEngine.h"
#include "../core/LocalServer.h"
#include "../core/Settings.h"
#include <shlwapi.h>
#include <shellapi.h>
#include <sstream>
#include <iomanip>

#define WM_TRAY_NOTIFY      (WM_APP + 100)
#define WM_EXT_DOWNLOAD     (WM_APP + 201)
#define WM_EXT_VIDEO        (WM_APP + 202)
#define ID_TIMER_UPDATE     1

namespace EasyDownload {

static const WCHAR* MAIN_CLASS = L"EasyDownloadMainClass";

MainWindow& MainWindow::Instance() {
    static MainWindow s_instance;
    return s_instance;
}

MainWindow::MainWindow() {}

MainWindow::~MainWindow() {
    RemoveTrayIcon();
    if (m_hFont) DeleteObject(m_hFont);
    if (m_hBoldFont) DeleteObject(m_hBoldFont);
}

bool MainWindow::Create() {
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = MAIN_CLASS;
    wc.hIcon = LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = MAKEINTRESOURCEW(IDR_MAIN_MENU);

    if (!RegisterClassExW(&wc)) return false;

    m_dpi = 96;
    int initW = 920;
    int initH = 580;

    m_hWnd = CreateWindowExW(
        WS_EX_WINDOWEDGE,
        MAIN_CLASS,
        L"EasyDownload - 고속 분할 다운로드 가속기",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        initW, initH,
        NULL, NULL,
        GetModuleHandle(NULL),
        this
    );

    if (!m_hWnd) return false;

    // High-DPI
    typedef UINT(WINAPI* PFN_GetDpiForWindow)(HWND);
    auto pGetDpiForWindow = (PFN_GetDpiForWindow)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (pGetDpiForWindow) {
        m_dpi = pGetDpiForWindow(m_hWnd);
    }
    UpdateDpiMetrics(m_dpi);

    InitControls();
    InitTrayIcon();

    // Hook LocalServer events
    LocalServer::Instance().SetOnDownloadRequest([this](const DownloadRequest& req) {
        auto pReq = new DownloadRequest(req);
        PostMessage(m_hWnd, WM_EXT_DOWNLOAD, (WPARAM)pReq, 0);
    });

    LocalServer::Instance().SetOnVideoDetected([this](const VideoDetectedInfo& info) {
        auto pInfo = new VideoDetectedInfo(info);
        PostMessage(m_hWnd, WM_EXT_VIDEO, (WPARAM)pInfo, 0);
    });

    // Start 250ms update timer
    SetTimer(m_hWnd, ID_TIMER_UPDATE, 250, NULL);

    RefreshTasksList();
    return true;
}

void MainWindow::Show(int nCmdShow) {
    if (Settings::Instance().GetStartMinimized()) {
        ShowWindow(m_hWnd, SW_HIDE);
    } else {
        ShowWindow(m_hWnd, nCmdShow);
        UpdateWindow(m_hWnd);
    }
}

void MainWindow::UpdateDpiMetrics(UINT dpi) {
    m_dpi = dpi;
    if (m_hFont) DeleteObject(m_hFont);
    if (m_hBoldFont) DeleteObject(m_hBoldFont);

    int fontHeight = -MulDiv(9, m_dpi, 72);
    m_hFont = CreateFontW(fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    m_hBoldFont = CreateFontW(fontHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void MainWindow::InitControls() {
    HINSTANCE hInst = GetModuleHandle(NULL);

    // Create Toolbar
    m_hToolbar = CreateWindowExW(
        0, TOOLBARCLASSNAMEW, NULL,
        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_TOP | CCS_NODIVIDER,
        0, 0, 0, 0,
        m_hWnd, (HMENU)100, hInst, NULL
    );
    SendMessage(m_hToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessage(m_hToolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS);

    TBBUTTON buttons[] = {
        { I_IMAGENONE, ID_TASK_NEW, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE, {0}, 0, (INT_PTR)L"➕ 새 다운로드" },
        { I_IMAGENONE, ID_TASK_NEW_VIDEO, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE, {0}, 0, (INT_PTR)L"🎬 비디오 다운로드" },
        { 0, 0, 0, BTNS_SEP, {0}, 0, 0 },
        { I_IMAGENONE, ID_TASK_START, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE, {0}, 0, (INT_PTR)L"▶ 시작" },
        { I_IMAGENONE, ID_TASK_PAUSE, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE, {0}, 0, (INT_PTR)L"⏸ 일시정지" },
        { I_IMAGENONE, ID_TASK_DELETE, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE, {0}, 0, (INT_PTR)L"🗑 삭제" },
        { 0, 0, 0, BTNS_SEP, {0}, 0, 0 },
        { I_IMAGENONE, ID_TASK_OPEN_FOLDER, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE, {0}, 0, (INT_PTR)L"📁 폴더 열기" },
        { I_IMAGENONE, ID_TASK_SETTINGS, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE, {0}, 0, (INT_PTR)L"⚙ 설정" },
        { 0, 0, 0, BTNS_SEP, {0}, 0, 0 },
        { I_IMAGENONE, ID_EXTENSION_INSTALL, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE, {0}, 0, (INT_PTR)L"🧩 크롬 확장 설치" },
    };
    SendMessage(m_hToolbar, TB_ADDBUTTONS, _countof(buttons), (LPARAM)buttons);
    SendMessage(m_hToolbar, TB_AUTOSIZE, 0, 0);

    // Create ListView (Multi-select enabled: LVS_SINGLESEL removed)
    m_hListView = CreateWindowExW(
        0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | WS_BORDER,
        0, 0, 0, 0,
        m_hWnd, (HMENU)200, hInst, NULL
    );
    ListView_SetExtendedListViewStyle(m_hListView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    SendMessage(m_hListView, WM_SETFONT, (WPARAM)m_hFont, TRUE);

    // Setup Columns
    struct ColDef { const WCHAR* name; int width; };
    ColDef cols[] = {
        { L"파일명", 200 },
        { L"크기", 85 },
        { L"진행률", 80 },
        { L"속도", 90 },
        { L"남은 시간", 85 },
        { L"상태", 85 },
        { L"분할 수", 60 },
        { L"URL", 250 },
        { L"추가 일시", 130 },
    };

    for (int i = 0; i < _countof(cols); ++i) {
        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        lvc.pszText = (LPWSTR)cols[i].name;
        lvc.cx = MulDiv(cols[i].width, m_dpi, 96);
        lvc.iSubItem = i;
        ListView_InsertColumn(m_hListView, i, &lvc);
    }

    // Create SegmentBar
    m_hSegmentBar = SegmentBar::Create(m_hWnd, 0, 0, 100, 32, 300);

    // Create StatusBar
    m_hStatusBar = CreateWindowExW(
        0, STATUSCLASSNAMEW, NULL,
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0,
        m_hWnd, (HMENU)400, hInst, NULL
    );
    SendMessage(m_hStatusBar, WM_SETFONT, (WPARAM)m_hFont, TRUE);

    int parts[] = { 300, 550, -1 };
    SendMessage(m_hStatusBar, SB_SETPARTS, 3, (LPARAM)parts);
}

void MainWindow::Relayout(int width, int height) {
    if (!m_hToolbar || !m_hListView || !m_hSegmentBar || !m_hStatusBar) return;

    SendMessage(m_hToolbar, TB_AUTOSIZE, 0, 0);
    RECT tbRect = { 0 };
    GetWindowRect(m_hToolbar, &tbRect);
    int tbHeight = tbRect.bottom - tbRect.top;

    SendMessage(m_hStatusBar, WM_SIZE, 0, 0);
    RECT sbRect = { 0 };
    GetWindowRect(m_hStatusBar, &sbRect);
    int sbHeight = sbRect.bottom - sbRect.top;

    int segHeight = MulDiv(32, m_dpi, 96);
    int segY = height - sbHeight - segHeight;
    SetWindowPos(m_hSegmentBar, NULL, 0, segY, width, segHeight, SWP_NOZORDER);

    int lvHeight = segY - tbHeight;
    SetWindowPos(m_hListView, NULL, 0, tbHeight, width, lvHeight > 0 ? lvHeight : 0, SWP_NOZORDER);
}

void MainWindow::RefreshTasksList() {
    auto tasks = DownloadManager::Instance().GetAllTasks();

    // Check count matching to avoid rebuilding if already in sync
    int currentItemCount = ListView_GetItemCount(m_hListView);
    if (currentItemCount != (int)tasks.size()) {
        ListView_DeleteAllItems(m_hListView);

        for (int i = 0; i < (int)tasks.size(); ++i) {
            const auto& t = tasks[i];
            LVITEMW lvi = { 0 };
            lvi.mask = LVIF_TEXT | LVIF_PARAM;
            lvi.iItem = i;
            lvi.pszText = (LPWSTR)t->GetFilename().c_str();
            lvi.lParam = (LPARAM)t.get();
            ListView_InsertItem(m_hListView, &lvi);
        }
    }

    UpdateTasksRealtime();
}

void MainWindow::UpdateTasksRealtime() {
    auto tasks = DownloadManager::Instance().GetAllTasks();
    int itemCount = ListView_GetItemCount(m_hListView);

    int64_t totalSpeed = 0;
    int downloadingCount = 0;

    for (int i = 0; i < itemCount && i < (int)tasks.size(); ++i) {
        const auto& t = tasks[i];

        int64_t total = t->GetTotalBytes();
        int64_t down = t->GetDownloadedBytes();
        int64_t speed = t->GetCurrentSpeed();
        totalSpeed += speed;

        if (t->GetStatus() == TaskStatus::Downloading || t->GetStatus() == TaskStatus::Connecting) {
            downloadingCount++;
        }

        // Col 0: Filename
        ListView_SetItemText(m_hListView, i, 0, (LPWSTR)t->GetFilename().c_str());

        // Col 1: Size
        std::wstring sizeStr = FormatBytes(total);
        ListView_SetItemText(m_hListView, i, 1, (LPWSTR)sizeStr.c_str());

        // Col 2: Progress
        std::wstring progStr;
        if (total > 0) {
            double pct = (double)down / (double)total * 100.0;
            if (pct > 100.0) pct = 100.0;
            std::wstringstream ss;
            ss << std::fixed << std::setprecision(1) << pct << L"%";
            progStr = ss.str();
        } else if (down > 0) {
            progStr = FormatBytes(down);
        } else {
            progStr = L"0.0%";
        }
        ListView_SetItemText(m_hListView, i, 2, (LPWSTR)progStr.c_str());

        // Col 3: Speed
        std::wstring speedStr = (t->GetStatus() == TaskStatus::Downloading) ? FormatSpeed(speed) : L"-";
        ListView_SetItemText(m_hListView, i, 3, (LPWSTR)speedStr.c_str());

        // Col 4: ETA
        std::wstring etaStr = (t->GetStatus() == TaskStatus::Downloading) ? FormatEta(t->GetRemainingSeconds()) : L"-";
        ListView_SetItemText(m_hListView, i, 4, (LPWSTR)etaStr.c_str());

        // Col 5: Status
        ListView_SetItemText(m_hListView, i, 5, (LPWSTR)t->GetStatusString().c_str());

        // Col 6: Parts
        std::wstring partsStr = std::to_wstring(t->GetNumParts()) + L" 개";
        ListView_SetItemText(m_hListView, i, 6, (LPWSTR)partsStr.c_str());

        // Col 7: URL
        ListView_SetItemText(m_hListView, i, 7, (LPWSTR)t->GetUrl().c_str());

        // Col 8: CreatedAt
        ListView_SetItemText(m_hListView, i, 8, (LPWSTR)t->GetCreatedAt().c_str());
    }

    // Update Status bar
    std::wstring part0 = L"작업: 총 " + std::to_wstring(tasks.size()) + L" 개 (" + std::to_wstring(downloadingCount) + L" 개 진행 중)";
    std::wstring part1 = L"전체 속도: " + FormatSpeed(totalSpeed);
    std::wstring part2 = L"크롬 브리지: 127.0.0.1:" + std::to_wstring(LocalServer::Instance().GetPort()) + L" (정상 작동)";

    SendMessageW(m_hStatusBar, SB_SETTEXTW, 0, (LPARAM)part0.c_str());
    SendMessageW(m_hStatusBar, SB_SETTEXTW, 1, (LPARAM)part1.c_str());
    SendMessageW(m_hStatusBar, SB_SETTEXTW, 2, (LPARAM)part2.c_str());

    UpdateSegmentBar();
}

void MainWindow::UpdateSegmentBar() {
    int sel = ListView_GetNextItem(m_hListView, -1, LVNI_SELECTED);
    auto tasks = DownloadManager::Instance().GetAllTasks();

    if (sel >= 0 && sel < (int)tasks.size()) {
        const auto& t = tasks[sel];
        SegmentBar::SetChunks(m_hSegmentBar, t->GetChunksProgress(), t->GetTotalBytes());
        return;
    }

    // If no row is selected, automatically show the active/downloading task!
    for (const auto& t : tasks) {
        if (t->GetStatus() == TaskStatus::Downloading || t->GetStatus() == TaskStatus::Connecting) {
            SegmentBar::SetChunks(m_hSegmentBar, t->GetChunksProgress(), t->GetTotalBytes());
            return;
        }
    }

    SegmentBar::Clear(m_hSegmentBar);
}

void MainWindow::InitTrayIcon() {
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = m_hWnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAY_NOTIFY;
    m_nid.hIcon = LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDI_APP_ICON));
    wcscpy_s(m_nid.szTip, _countof(m_nid.szTip), L"EasyDownload - 가속기");

    Shell_NotifyIconW(NIM_ADD, &m_nid);
    m_trayActive = true;
}

void MainWindow::RemoveTrayIcon() {
    if (m_trayActive) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_trayActive = false;
    }
}

void MainWindow::ShowTrayNotification(const std::wstring& title, const std::wstring& message) {
    m_nid.uFlags |= NIF_INFO;
    m_nid.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    wcscpy_s(m_nid.szInfoTitle, _countof(m_nid.szInfoTitle), title.c_str());
    wcscpy_s(m_nid.szInfo, _countof(m_nid.szInfo), message.c_str());
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void MainWindow::OnNewDownload() {
    std::wstring url, savePath;
    int parts = Settings::Instance().GetInitialParts();
    bool immediate = true;

    if (NewDownloadDialog::Show(m_hWnd, url, savePath, parts, immediate)) {
        auto task = DownloadManager::Instance().AddTask(url, savePath, L"", L"", parts, immediate);
        RefreshTasksList();
        if (task) {
            DownloadProgressDialog::Show(m_hWnd, task);
        }
    }
}

void MainWindow::OnNewVideoDownload(
    const std::wstring& initUrl,
    const std::wstring& initTitle,
    const std::wstring& referer,
    const std::wstring& cookies
) {
    std::wstring streamUrl, savePath;
    if (VideoDownloadDialog::Show(m_hWnd, streamUrl, savePath, initUrl, initTitle, referer, cookies)) {
        auto task = DownloadManager::Instance().AddTask(streamUrl, savePath, referer, cookies, 8, true);
        if (task) {
            if (streamUrl.find(L".m3u8") != std::wstring::npos) {
                task->SetIsHls(true);
            }
            RefreshTasksList();
            DownloadProgressDialog::Show(m_hWnd, task);
        }
    }
}

void MainWindow::OnStartSelected() {
    auto tasks = DownloadManager::Instance().GetAllTasks();
    std::vector<std::wstring> taskIds;
    int item = -1;
    while ((item = ListView_GetNextItem(m_hListView, item, LVNI_SELECTED)) != -1) {
        if (item >= 0 && item < (int)tasks.size()) {
            taskIds.push_back(tasks[item]->GetId());
        }
    }
    for (const auto& id : taskIds) {
        DownloadManager::Instance().StartTask(id);
    }
}

void MainWindow::OnPauseSelected() {
    auto tasks = DownloadManager::Instance().GetAllTasks();
    std::vector<std::wstring> taskIds;
    int item = -1;
    while ((item = ListView_GetNextItem(m_hListView, item, LVNI_SELECTED)) != -1) {
        if (item >= 0 && item < (int)tasks.size()) {
            taskIds.push_back(tasks[item]->GetId());
        }
    }
    for (const auto& id : taskIds) {
        DownloadManager::Instance().PauseTask(id);
    }
}

void MainWindow::OnDeleteSelected() {
    auto tasks = DownloadManager::Instance().GetAllTasks();
    std::vector<std::wstring> taskIds;
    int item = -1;
    while ((item = ListView_GetNextItem(m_hListView, item, LVNI_SELECTED)) != -1) {
        if (item >= 0 && item < (int)tasks.size()) {
            taskIds.push_back(tasks[item]->GetId());
        }
    }
    if (taskIds.empty()) return;

    // Delete only from download list; do not delete physical files on disk
    for (const auto& id : taskIds) {
        DownloadManager::Instance().DeleteTask(id, false);
    }
    RefreshTasksList();
}

void MainWindow::OnOpenSelectedFolder() {
    int sel = ListView_GetNextItem(m_hListView, -1, LVNI_SELECTED);
    if (sel >= 0) {
        auto tasks = DownloadManager::Instance().GetAllTasks();
        if (sel < (int)tasks.size()) {
            std::wstring path = tasks[sel]->GetSavePath();
            std::wstring param = L"/select,\"" + path + L"\"";
            ShellExecuteW(NULL, L"open", L"explorer.exe", param.c_str(), NULL, SW_SHOWNORMAL);
        }
    }
}

void MainWindow::OnOpenSelectedFile() {
    int sel = ListView_GetNextItem(m_hListView, -1, LVNI_SELECTED);
    if (sel >= 0) {
        auto tasks = DownloadManager::Instance().GetAllTasks();
        if (sel < (int)tasks.size()) {
            std::wstring path = tasks[sel]->GetSavePath();
            ShellExecuteW(NULL, L"open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }
    }
}

void MainWindow::OnCopySelectedUrl() {
    int sel = ListView_GetNextItem(m_hListView, -1, LVNI_SELECTED);
    if (sel >= 0) {
        auto tasks = DownloadManager::Instance().GetAllTasks();
        if (sel < (int)tasks.size()) {
            std::wstring url = tasks[sel]->GetUrl();
            if (!url.empty() && OpenClipboard(m_hWnd)) {
                EmptyClipboard();
                size_t bytes = (url.length() + 1) * sizeof(WCHAR);
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
                if (hMem) {
                    memcpy(GlobalLock(hMem), url.c_str(), bytes);
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_UNICODETEXT, hMem);
                }
                CloseClipboard();
            }
        }
    }
}

void MainWindow::OnShowSelectedInfo() {
    int sel = ListView_GetNextItem(m_hListView, -1, LVNI_SELECTED);
    if (sel >= 0) {
        auto tasks = DownloadManager::Instance().GetAllTasks();
        if (sel < (int)tasks.size()) {
            auto task = tasks[sel];
            if (task->GetStatus() == TaskStatus::Completed) {
                DownloadInfoDialog::Show(m_hWnd, task);
            } else {
                DownloadProgressDialog::Show(m_hWnd, task);
            }
        }
    }
}

void MainWindow::OnSettings() {
    SettingsDialog::Show(m_hWnd);
}

void MainWindow::OnInstallExtension() {
    // 1. Get absolute path of extension folder
    WCHAR exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);

    // If running in build output or root
    std::wstring extDir = std::wstring(exePath) + L"\\extension";
    if (!PathFileExistsW(extDir.c_str())) {
        // Try parent directory
        WCHAR parentDir[MAX_PATH] = { 0 };
        wcscpy_s(parentDir, exePath);
        PathRemoveFileSpecW(parentDir);
        extDir = std::wstring(parentDir) + L"\\extension";
    }

    std::wstring msg = L"Chrome 확장 프로그램 연동 안내:\n\n"
                       L"1. Chrome 브라우저 주소창에 chrome://extensions 를 입력하여 이동합니다.\n"
                       L"2. 우측 상단의 '개발자 모드' 스위치를 켭니다.\n"
                       L"3. '압축해제된 확장 프로그램을 로드합니다' 버튼을 클릭합니다.\n"
                       L"4. 지금 열리는 확장 폴더를 선택하시면 즉시 등록됩니다!\n\n"
                       L"확인 버튼을 누르면 확장 폴더와 브라우저 설정 페이지가 열립니다.";

    if (MessageBoxW(m_hWnd, msg.c_str(), L"크롬 확장 프로그램 설치", MB_OKCANCEL | MB_ICONINFORMATION) == IDOK) {
        ShellExecuteW(NULL, L"open", extDir.c_str(), NULL, NULL, SW_SHOWNORMAL);
        ShellExecuteW(NULL, L"open", L"chrome://extensions", NULL, NULL, SW_SHOWNORMAL);
    }
}

LRESULT CALLBACK MainWindow::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    MainWindow* pThis = nullptr;
    if (uMsg == WM_CREATE) {
        auto cs = (CREATESTRUCT*)lParam;
        pThis = (MainWindow*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
    } else {
        pThis = (MainWindow*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    }

    if (pThis) {
        return pThis->HandleMessage(uMsg, wParam, lParam);
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_SIZE: {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        Relayout(w, h);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = MulDiv(640, m_dpi, 96);
        mmi->ptMinTrackSize.y = MulDiv(400, m_dpi, 96);
        return 0;
    }
    case WM_DPICHANGED: {
        UINT newDpi = HIWORD(wParam);
        RECT* prc = (RECT*)lParam;
        UpdateDpiMetrics(newDpi);
        SetWindowPos(m_hWnd, NULL, prc->left, prc->top, prc->right - prc->left, prc->bottom - prc->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_TIMER: {
        if (wParam == ID_TIMER_UPDATE) {
            UpdateTasksRealtime();
        }
        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        switch (id) {
        case ID_TASK_NEW: OnNewDownload(); break;
        case ID_TASK_NEW_VIDEO: OnNewVideoDownload(); break;
        case ID_TASK_START: OnStartSelected(); break;
        case ID_TASK_PAUSE: OnPauseSelected(); break;
        case ID_TASK_DELETE: OnDeleteSelected(); break;
        case ID_TASK_OPEN_FOLDER: OnOpenSelectedFolder(); break;
        case ID_TASK_OPEN_FILE: OnOpenSelectedFile(); break;
        case ID_TASK_COPY_URL: OnCopySelectedUrl(); break;
        case ID_TASK_SHOW_INFO: OnShowSelectedInfo(); break;
        case ID_TASK_SETTINGS: OnSettings(); break;
        case ID_EXTENSION_INSTALL: OnInstallExtension(); break;
        case ID_APP_EXIT: DestroyWindow(m_hWnd); break;
        case ID_TRAY_SHOW:
            ShowWindow(m_hWnd, SW_RESTORE);
            SetForegroundWindow(m_hWnd);
            break;
        case ID_TRAY_EXIT:
            DestroyWindow(m_hWnd);
            break;
        case ID_ABOUT:
            MessageBoxW(m_hWnd, L"EasyDownload v1.0.0\n\n초고속 멀티스레드 분할 다운로드 가속기\n크롬 확장 연동 및 비디오 스트림 다운로더",
                L"EasyDownload 정보", MB_OK | MB_ICONINFORMATION);
            break;
        }
        return 0;
    }
    case WM_NOTIFY: {
        auto nm = (NMHDR*)lParam;
        if (nm->idFrom == 200) { // ListView
            if (nm->code == LVN_ITEMCHANGED) {
                UpdateSegmentBar();
            } else if (nm->code == LVN_KEYDOWN) {
                auto pnmk = (NMLVKEYDOWN*)lParam;
                if (pnmk->wVKey == VK_DELETE) {
                    OnDeleteSelected();
                    return TRUE;
                }
            } else if (nm->code == NM_DBLCLK) {
                int sel = ListView_GetNextItem(m_hListView, -1, LVNI_SELECTED);
                if (sel >= 0) {
                    auto tasks = DownloadManager::Instance().GetAllTasks();
                    if (sel < (int)tasks.size()) {
                        auto task = tasks[sel];
                        if (task->GetStatus() == TaskStatus::Completed) {
                            OnOpenSelectedFile();
                        } else {
                            DownloadProgressDialog::Show(m_hWnd, task);
                        }
                    }
                }
            } else if (nm->code == NM_RCLICK) {
                POINT pt;
                GetCursorPos(&pt);
                POINT clientPt = pt;
                ScreenToClient(m_hListView, &clientPt);
                LVHITTESTINFO lvhti = { 0 };
                lvhti.pt = clientPt;
                int hit = ListView_HitTest(m_hListView, &lvhti);
                if (hit >= 0) {
                    int state = ListView_GetItemState(m_hListView, hit, LVIS_SELECTED);
                    if (!(state & LVIS_SELECTED)) {
                        ListView_SetItemState(m_hListView, hit, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    }

                    HMENU hMenu = CreatePopupMenu();
                    AppendMenuW(hMenu, MF_STRING, ID_TASK_OPEN_FILE, L"열기(&O)");
                    AppendMenuW(hMenu, MF_STRING, ID_TASK_OPEN_FOLDER, L"폴더 열기(&F)");
                    AppendMenuW(hMenu, MF_STRING, ID_TASK_COPY_URL, L"다운로드 주소 복사(&C)");
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                    AppendMenuW(hMenu, MF_STRING, ID_TASK_SHOW_INFO, L"다운로드 정보 / 진행 창(&I)...");

                    SetForegroundWindow(m_hWnd);
                    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hWnd, NULL);
                    DestroyMenu(hMenu);
                }
            }
        }
        break;
    }
    case WM_EXT_DOWNLOAD: {
        auto req = (DownloadRequest*)wParam;
        if (req) {
            // Bring client window to foreground over browser
            ForceForegroundWindow(m_hWnd);

            std::wstring fname = req->filename;
            if (fname.empty()) {
                fname = HttpEngine::ExtractFilenameFromUrl(req->url);
            }
            std::wstring baseDir = Settings::Instance().GetTargetFolderForFilename(fname);
            WCHAR combined[MAX_PATH] = { 0 };
            PathCombineW(combined, baseDir.c_str(), fname.c_str());

            // Auto add task from Chrome with resolved category path!
            auto task = DownloadManager::Instance().AddTask(req->url, combined, req->referer, req->cookies, 8, true);
            RefreshTasksList();
            if (task) {
                DownloadProgressDialog::Show(m_hWnd, task);
            }
            delete req;
        }
        return 0;
    }
    case WM_EXT_VIDEO: {
        auto info = (VideoDetectedInfo*)wParam;
        if (info) {
            // Bring client window to foreground over browser
            ForceForegroundWindow(m_hWnd);

            std::wstring fname = HttpEngine::SanitizeFilename(info->title);
            if (fname.empty()) {
                fname = HttpEngine::ExtractFilenameFromUrl(info->videoUrl);
            }
            if (fname.empty()) {
                fname = L"video_stream";
            }
            if (fname.rfind(L".ts") == std::wstring::npos && fname.rfind(L".mp4") == std::wstring::npos) {
                fname += (info->videoUrl.find(L".m3u8") != std::wstring::npos) ? L".ts" : L".mp4";
            }

            std::wstring baseDir = Settings::Instance().GetTargetFolderForFilename(fname);
            WCHAR combined[MAX_PATH] = { 0 };
            PathCombineW(combined, baseDir.c_str(), fname.c_str());

            // Automatically start video download immediately without requiring extra dialog clicks!
            auto task = DownloadManager::Instance().AddTask(info->videoUrl, combined, info->referer, info->cookies, 8, true);
            RefreshTasksList();
            if (task) {
                DownloadProgressDialog::Show(m_hWnd, task);
            }
            delete info;
        }
        return 0;
    }
    case WM_TRAY_NOTIFY: {
        if (lParam == WM_LBUTTONDBLCLK) {
            ShowWindow(m_hWnd, SW_RESTORE);
            SetForegroundWindow(m_hWnd);
        } else if (lParam == WM_RBUTTONUP) {
            HMENU hMenu = LoadMenuW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDR_TRAY_MENU));
            if (hMenu) {
                HMENU hSub = GetSubMenu(hMenu, 0);
                POINT pt;
                GetCursorPos(&pt);
                SetForegroundWindow(m_hWnd);
                TrackPopupMenu(hSub, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hWnd, NULL);
                DestroyMenu(hMenu);
            }
        }
        return 0;
    }
    case WM_CLOSE: {
        ShowWindow(m_hWnd, SW_HIDE);
        return 0;
    }
    case WM_DESTROY: {
        KillTimer(m_hWnd, ID_TIMER_UPDATE);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(m_hWnd, uMsg, wParam, lParam);
}

} // namespace EasyDownload
