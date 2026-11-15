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
#include "../core/DpiBypassManager.h"
#include "../core/HlsDownloader.h"
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
    DpiBypassManager::Instance().Stop();
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
        L"EasyDownload",
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

    // Auto start GoodbyeDPI bypass if enabled (default: true)
    if (Settings::Instance().GetDpiBypassEnabled()) {
        DpiBypassManager::Instance().Start(Settings::Instance().GetDpiBypassMode());
    }
    UpdateDpiBypassUi();

    // Auto update yt-dlp in background on application startup
    HlsDownloader::UpdateYtDlpInBackground();

    // Ensure Task Scheduler auto-start is registered with highest privileges if enabled
    if (Settings::Instance().GetAutoStartWithWindows()) {
        Settings::Instance().SetAutoStartWithWindows(true);
    }

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
        { I_IMAGENONE, ID_TASK_BATCH_EXTRACT, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE, {0}, 0, (INT_PTR)L"🔗 일괄 링크 추출" },
        { 0, 0, 0, BTNS_SEP, {0}, 0, 0 },
        { I_IMAGENONE, ID_TASK_START, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE, {0}, 0, (INT_PTR)L"▶ 시작 / 재시작" },
        { I_IMAGENONE, ID_TASK_PAUSE, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE, {0}, 0, (INT_PTR)L"⏸ 중단" },
        { I_IMAGENONE, ID_TASK_DELETE, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE, {0}, 0, (INT_PTR)L"🗑 삭제" },
        { 0, 0, 0, BTNS_SEP, {0}, 0, 0 },
        { I_IMAGENONE, ID_TASK_OPEN_FOLDER, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE, {0}, 0, (INT_PTR)L"📁 폴더 열기" },
        { I_IMAGENONE, ID_TASK_SETTINGS, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE, {0}, 0, (INT_PTR)L"⚙ 설정" },
        { 0, 0, 0, BTNS_SEP, {0}, 0, 0 },
        { I_IMAGENONE, ID_EXTENSION_INSTALL, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE, {0}, 0, (INT_PTR)L"🧩 크롬 확장 설치" },
    };
    SendMessage(m_hToolbar, TB_ADDBUTTONS, _countof(buttons), (LPARAM)buttons);
    SendMessage(m_hToolbar, TB_AUTOSIZE, 0, 0);

    // Create Main Tab Control
    m_hTabCtrl = CreateWindowExW(
        0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 0, 0,
        m_hWnd, (HMENU)IDC_MAIN_TAB, hInst, NULL
    );
    SendMessage(m_hTabCtrl, WM_SETFONT, (WPARAM)m_hFont, TRUE);

    TCITEMW tie = { 0 };
    tie.mask = TCIF_TEXT;
    tie.pszText = (LPWSTR)L"📥 다운로드 목록";
    TabCtrl_InsertItem(m_hTabCtrl, 0, &tie);
    tie.pszText = (LPWSTR)L"🔗 일괄 링크 추출기";
    TabCtrl_InsertItem(m_hTabCtrl, 1, &tie);

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

    int parts[] = { 260, 440, 680, -1 };
    SendMessage(m_hStatusBar, SB_SETPARTS, 4, (LPARAM)parts);

    InitBatchControls();
}

void MainWindow::InitBatchControls() {
    HINSTANCE hInst = GetModuleHandle(NULL);

    m_hBatchGuideLbl = CreateWindowExW(
        0, L"STATIC", L"다운로드 링크가 포함된 본문 텍스트(마크다운, 웹페이지 복사 내용 등)를 아래에 붙여넣으세요:",
        WS_CHILD | SS_LEFTNOWORDWRAP,
        0, 0, 0, 0, m_hWnd, (HMENU)IDC_LBL_BATCH_GUIDE, hInst, NULL
    );
    SendMessage(m_hBatchGuideLbl, WM_SETFONT, (WPARAM)m_hBoldFont, TRUE);

    m_hBatchTextEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
        0, 0, 0, 0, m_hWnd, (HMENU)IDC_EDT_BATCH_INPUT, hInst, NULL
    );
    SendMessage(m_hBatchTextEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);

    m_hBatchHostLbl = CreateWindowExW(
        0, L"STATIC", L"호스트 우선순위:",
        WS_CHILD | SS_LEFTNOWORDWRAP,
        0, 0, 0, 0, m_hWnd, (HMENU)IDC_LBL_BATCH_HOST, hInst, NULL
    );
    SendMessage(m_hBatchHostLbl, WM_SETFONT, (WPARAM)m_hFont, TRUE);

    m_hBatchHostCombo = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, 0, 0, 0, m_hWnd, (HMENU)IDC_CMB_BATCH_HOST, hInst, NULL
    );
    SendMessage(m_hBatchHostCombo, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    SendMessageW(m_hBatchHostCombo, CB_ADDSTRING, 0, (LPARAM)L"모든 호스트 (All)");
    SendMessageW(m_hBatchHostCombo, CB_ADDSTRING, 0, (LPARAM)L"Send.now 우선");
    SendMessageW(m_hBatchHostCombo, CB_ADDSTRING, 0, (LPARAM)L"1Fichier 우선");
    SendMessageW(m_hBatchHostCombo, CB_ADDSTRING, 0, (LPARAM)L"Megaup 우선");
    SendMessageW(m_hBatchHostCombo, CB_SETCURSEL, 0, 0);

    m_hBatchSeqCheck = CreateWindowExW(
        0, L"BUTTON", L"순차 다운로드 (1개 완료 시 다음 파트 자동 시작 - 호스트 차단 방지)",
        WS_CHILD | BS_AUTOCHECKBOX,
        0, 0, 0, 0, m_hWnd, (HMENU)IDC_CHK_BATCH_SEQUENTIAL, hInst, NULL
    );
    SendMessage(m_hBatchSeqCheck, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    SendMessage(m_hBatchSeqCheck, BM_SETCHECK, BST_CHECKED, 0);

    m_hBatchBtnAnalyze = CreateWindowExW(
        0, L"BUTTON", L"🔍 링크 분석 및 최종 주소 추출",
        WS_CHILD | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hWnd, (HMENU)IDC_BTN_BATCH_ANALYZE, hInst, NULL
    );
    SendMessage(m_hBatchBtnAnalyze, WM_SETFONT, (WPARAM)m_hBoldFont, TRUE);

    m_hBatchListView = CreateWindowExW(
        0, WC_LISTVIEWW, L"",
        WS_CHILD | LVS_REPORT | LVS_SHOWSELALWAYS | WS_BORDER,
        0, 0, 0, 0, m_hWnd, (HMENU)IDC_LST_BATCH_RESULTS, hInst, NULL
    );
    ListView_SetExtendedListViewStyle(m_hBatchListView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_CHECKBOXES);
    SendMessage(m_hBatchListView, WM_SETFONT, (WPARAM)m_hFont, TRUE);

    struct BatchColDef { const WCHAR* name; int width; };
    BatchColDef batchCols[] = {
        { L"파트 / 라벨", 150 },
        { L"호스트", 90 },
        { L"용량", 80 },
        { L"상태", 180 },
        { L"파일명", 240 },
        { L"최종 직링크", 280 },
        { L"원본 URL", 240 },
    };

    for (int i = 0; i < _countof(batchCols); ++i) {
        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        lvc.pszText = (LPWSTR)batchCols[i].name;
        lvc.cx = MulDiv(batchCols[i].width, m_dpi, 96);
        lvc.iSubItem = i;
        ListView_InsertColumn(m_hBatchListView, i, &lvc);
    }

    m_hBatchBtnAddAll = CreateWindowExW(
        0, L"BUTTON", L"⬇️ 전체 항목 다운로드 추가 (순차 시작)",
        WS_CHILD | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hWnd, (HMENU)IDC_BTN_BATCH_ADD_ALL, hInst, NULL
    );
    SendMessage(m_hBatchBtnAddAll, WM_SETFONT, (WPARAM)m_hBoldFont, TRUE);

    m_hBatchBtnAddSelected = CreateWindowExW(
        0, L"BUTTON", L"⬇️ 선택 항목만 추가",
        WS_CHILD | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hWnd, (HMENU)IDC_BTN_BATCH_ADD_SELECTED, hInst, NULL
    );
    SendMessage(m_hBatchBtnAddSelected, WM_SETFONT, (WPARAM)m_hFont, TRUE);

    m_hBatchBtnClear = CreateWindowExW(
        0, L"BUTTON", L"🧹 초기화",
        WS_CHILD | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hWnd, (HMENU)IDC_BTN_BATCH_CLEAR, hInst, NULL
    );
    SendMessage(m_hBatchBtnClear, WM_SETFONT, (WPARAM)m_hFont, TRUE);

    m_hBatchStatusLbl = CreateWindowExW(
        0, L"STATIC", L"준비 완료: 텍스트를 붙여넣고 [🔍 링크 분석 및 최종 주소 추출] 버튼을 누르세요.",
        WS_CHILD | SS_LEFTNOWORDWRAP,
        0, 0, 0, 0, m_hWnd, (HMENU)IDC_LBL_BATCH_STATUS, hInst, NULL
    );
    SendMessage(m_hBatchStatusLbl, WM_SETFONT, (WPARAM)m_hFont, TRUE);
}

void MainWindow::SwitchTab(int tabIndex) {
    m_currentTab = tabIndex;
    TabCtrl_SetCurSel(m_hTabCtrl, tabIndex);

    int showDownloads = (tabIndex == 0) ? SW_SHOW : SW_HIDE;
    int showBatch = (tabIndex == 1) ? SW_SHOW : SW_HIDE;

    ShowWindow(m_hListView, showDownloads);
    ShowWindow(m_hSegmentBar, showDownloads);

    ShowWindow(m_hBatchGuideLbl, showBatch);
    ShowWindow(m_hBatchTextEdit, showBatch);
    ShowWindow(m_hBatchHostLbl, showBatch);
    ShowWindow(m_hBatchHostCombo, showBatch);
    ShowWindow(m_hBatchSeqCheck, showBatch);
    ShowWindow(m_hBatchBtnAnalyze, showBatch);
    ShowWindow(m_hBatchListView, showBatch);
    ShowWindow(m_hBatchBtnAddSelected, showBatch);
    ShowWindow(m_hBatchBtnAddAll, showBatch);
    ShowWindow(m_hBatchBtnClear, showBatch);
    ShowWindow(m_hBatchStatusLbl, showBatch);

    RECT rc;
    GetClientRect(m_hWnd, &rc);
    Relayout(rc.right - rc.left, rc.bottom - rc.top);
    InvalidateRect(m_hWnd, NULL, TRUE);

    if (tabIndex == 0) {
        RefreshTasksList();
    }
}

void MainWindow::Relayout(int width, int height) {
    if (!m_hToolbar || !m_hTabCtrl || !m_hListView || !m_hSegmentBar || !m_hStatusBar) return;

    SendMessage(m_hToolbar, TB_AUTOSIZE, 0, 0);
    RECT tbRect = { 0 };
    GetWindowRect(m_hToolbar, &tbRect);
    int tbHeight = tbRect.bottom - tbRect.top;

    SendMessage(m_hStatusBar, WM_SIZE, 0, 0);
    RECT sbRect = { 0 };
    GetWindowRect(m_hStatusBar, &sbRect);
    int sbHeight = sbRect.bottom - sbRect.top;

    int tabHeight = MulDiv(28, m_dpi, 96);
    SetWindowPos(m_hTabCtrl, NULL, 0, tbHeight, width, tabHeight, SWP_NOZORDER);

    int contentTop = tbHeight + tabHeight;
    int contentBottom = height - sbHeight;

    if (m_currentTab == 0) {
        int segHeight = MulDiv(32, m_dpi, 96);
        int segY = contentBottom - segHeight;
        SetWindowPos(m_hSegmentBar, NULL, 0, segY, width, segHeight, SWP_NOZORDER);

        int lvHeight = segY - contentTop;
        SetWindowPos(m_hListView, NULL, 0, contentTop, width, lvHeight > 0 ? lvHeight : 0, SWP_NOZORDER);
    } else {
        RelayoutBatch(width, contentTop, contentBottom);
    }
}

void MainWindow::RelayoutBatch(int width, int topY, int bottomY) {
    int pad = MulDiv(8, m_dpi, 96);
    int rowH = MulDiv(22, m_dpi, 96);
    int editH = MulDiv(110, m_dpi, 96);
    int btnH = MulDiv(28, m_dpi, 96);

    int y = topY + pad;
    int contentW = width - pad * 2;

    // 1. Guide Label
    SetWindowPos(m_hBatchGuideLbl, NULL, pad, y, contentW, rowH, SWP_NOZORDER);
    y += rowH;

    // 2. Multiline Text Edit
    SetWindowPos(m_hBatchTextEdit, NULL, pad, y, contentW, editH, SWP_NOZORDER);
    y += editH + pad;

    // 3. Options Bar: Host Label + Combo + Sequential Checkbox + Analyze Button
    int hostLblW = MulDiv(90, m_dpi, 96);
    int comboW = MulDiv(140, m_dpi, 96);
    int checkW = MulDiv(380, m_dpi, 96);
    int analyzeBtnW = MulDiv(210, m_dpi, 96);

    SetWindowPos(m_hBatchHostLbl, NULL, pad, y + 3, hostLblW, rowH, SWP_NOZORDER);
    SetWindowPos(m_hBatchHostCombo, NULL, pad + hostLblW, y, comboW, rowH * 8, SWP_NOZORDER);

    SetWindowPos(m_hBatchSeqCheck, NULL, pad + hostLblW + comboW + pad, y + 2, checkW, rowH, SWP_NOZORDER);

    int btnX = width - pad - analyzeBtnW;
    SetWindowPos(m_hBatchBtnAnalyze, NULL, btnX > 0 ? btnX : pad, y - 2, analyzeBtnW, btnH, SWP_NOZORDER);
    y += btnH + pad;

    // 4. Action buttons at bottom
    int bottomBtnY = bottomY - pad - btnH;
    int btnAddAllW = MulDiv(260, m_dpi, 96);
    int btnAddSelW = MulDiv(150, m_dpi, 96);
    int btnClearW = MulDiv(80, m_dpi, 96);

    SetWindowPos(m_hBatchBtnAddAll, NULL, pad, bottomBtnY, btnAddAllW, btnH, SWP_NOZORDER);
    SetWindowPos(m_hBatchBtnAddSelected, NULL, pad + btnAddAllW + pad, bottomBtnY, btnAddSelW, btnH, SWP_NOZORDER);
    SetWindowPos(m_hBatchBtnClear, NULL, pad + btnAddAllW + pad + btnAddSelW + pad, bottomBtnY, btnClearW, btnH, SWP_NOZORDER);

    // Status label at right of bottom buttons
    int statusLblX = pad + btnAddAllW + pad + btnAddSelW + pad + btnClearW + pad;
    int statusLblW = width - pad - statusLblX;
    SetWindowPos(m_hBatchStatusLbl, NULL, statusLblX, bottomBtnY + 5, statusLblW > 0 ? statusLblW : 0, rowH, SWP_NOZORDER);

    // 5. Results ListView in the remaining middle space
    int listH = bottomBtnY - pad - y;
    SetWindowPos(m_hBatchListView, NULL, pad, y, contentW, listH > 0 ? listH : 0, SWP_NOZORDER);
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
    SendMessageW(m_hStatusBar, SB_SETTEXTW, 3, (LPARAM)DpiBypassManager::Instance().GetStatusString().c_str());

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
    wcscpy_s(m_nid.szTip, _countof(m_nid.szTip), L"EasyDownload");

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

void MainWindow::OnToggleDpiBypass() {
    DpiBypassManager::Instance().Toggle();
    UpdateDpiBypassUi();
}

void MainWindow::UpdateDpiBypassUi() {
    SendMessageW(m_hStatusBar, SB_SETTEXTW, 3, (LPARAM)DpiBypassManager::Instance().GetStatusString().c_str());
}

void MainWindow::OnToggleChromeDoh() {
    bool current = DpiBypassManager::IsChromeDohConfigured();
    bool next = !current;
    if (DpiBypassManager::SetChromeDohEnabled(next)) {
        HMENU hMenu = GetMenu(m_hWnd);
        if (hMenu) {
            CheckMenuItem(hMenu, ID_CHROME_DOH_TOGGLE, MF_BYCOMMAND | (next ? MF_CHECKED : MF_UNCHECKED));
        }

        if (next) {
            MessageBoxW(m_hWnd,
                L"Chrome 보안 DNS (Cloudflare 1.1.1.1 DoH)가 활성화되었습니다.\n\n"
                L"통신사 DNS 차단이 우회되어 웹사이트 접속 지연 및 새로고침 문제가 해결됩니다.\n\n"
                L"* Chrome 브라우저를 완전히 닫고 다시 실행하면 즉시 적용됩니다.",
                L"보안 DNS 적용 완료", MB_OK | MB_ICONINFORMATION);
        } else {
            MessageBoxW(m_hWnd,
                L"Chrome 보안 DNS 설정이 해제되어 기본값으로 복원되었습니다.\n\n"
                L"* Chrome 브라우저를 완전히 닫고 다시 실행하면 원래대로 복구됩니다.",
                L"보안 DNS 복원 완료", MB_OK | MB_ICONINFORMATION);
        }
    } else {
        MessageBoxW(m_hWnd, L"레지스트리 설정 변경에 실패했습니다. 관리자 권한을 확인해 주세요.", L"오류", MB_OK | MB_ICONERROR);
    }
}

void MainWindow::OnBatchAnalyze() {
    if (m_batchAnalyzing) {
        m_batchAbortFlag = true;
        SetWindowTextW(m_hBatchBtnAnalyze, L"중단 요청 중...");
        EnableWindow(m_hBatchBtnAnalyze, FALSE);
        return;
    }

    int len = GetWindowTextLengthW(m_hBatchTextEdit);
    if (len <= 0) {
        MessageBoxW(m_hWnd, L"분석할 텍스트를 먼저 입력창에 붙여넣으세요.", L"안내", MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::wstring rawText(len + 1, 0);
    GetWindowTextW(m_hBatchTextEdit, &rawText[0], len + 1);
    rawText.resize(len);

    int hostSel = (int)SendMessageW(m_hBatchHostCombo, CB_GETCURSEL, 0, 0);
    std::wstring hostFilter;
    if (hostSel == 1) hostFilter = L"Send.now";
    else if (hostSel == 2) hostFilter = L"1Fichier";
    else if (hostSel == 3) hostFilter = L"Megaup";

    m_batchLinks = LinkExtractor::ParseRawText(rawText, hostFilter);
    if (m_batchLinks.empty()) {
        MessageBoxW(m_hWnd, L"입력한 텍스트에서 인식 가능한 다운로드 링크를 찾지 못했습니다.", L"안내", MB_OK | MB_ICONWARNING);
        return;
    }

    ListView_DeleteAllItems(m_hBatchListView);
    for (int i = 0; i < (int)m_batchLinks.size(); ++i) {
        const auto& link = m_batchLinks[i];
        LVITEMW lvi = { 0 };
        lvi.mask = LVIF_TEXT;
        lvi.iItem = i;
        lvi.pszText = (LPWSTR)link.label.c_str();
        ListView_InsertItem(m_hBatchListView, &lvi);

        ListView_SetCheckState(m_hBatchListView, i, TRUE);
        UpdateBatchListItem(i);
    }

    m_batchAnalyzing = true;
    m_batchAbortFlag = false;
    SetWindowTextW(m_hBatchBtnAnalyze, L"⏹ 분석 중단");
    SetWindowTextW(m_hBatchStatusLbl, (L"총 " + std::to_wstring(m_batchLinks.size()) + L" 개 링크 분석 및 직링크 추출 시작...").c_str());

    std::thread([this]() {
        int resolvedCount = 0;
        int failedCount = 0;

        for (size_t i = 0; i < m_batchLinks.size(); ++i) {
            if (m_batchAbortFlag.load()) break;

            m_batchLinks[i].status = LinkStatus::Resolving;
            m_batchLinks[i].statusMessage = L"분석 중...";
            PostMessageW(m_hWnd, WM_APP + 10, (WPARAM)i, 0);

            bool ok = LinkExtractor::ResolveLink(m_batchLinks[i], m_batchAbortFlag);
            if (ok) resolvedCount++;
            else failedCount++;

            PostMessageW(m_hWnd, WM_APP + 10, (WPARAM)i, 0);
        }

        m_batchAnalyzing = false;
        PostMessageW(m_hWnd, WM_APP + 11, (WPARAM)resolvedCount, (LPARAM)failedCount);
    }).detach();
}

void MainWindow::UpdateBatchListItem(int index) {
    if (index < 0 || index >= (int)m_batchLinks.size()) return;
    const auto& link = m_batchLinks[index];

    ListView_SetItemText(m_hBatchListView, index, 0, (LPWSTR)link.label.c_str());
    ListView_SetItemText(m_hBatchListView, index, 1, (LPWSTR)link.hostName.c_str());
    ListView_SetItemText(m_hBatchListView, index, 2, (LPWSTR)link.sizeStr.c_str());
    ListView_SetItemText(m_hBatchListView, index, 3, (LPWSTR)link.statusMessage.c_str());
    ListView_SetItemText(m_hBatchListView, index, 4, (LPWSTR)link.filename.c_str());
    ListView_SetItemText(m_hBatchListView, index, 5, (LPWSTR)link.directUrl.c_str());
    ListView_SetItemText(m_hBatchListView, index, 6, (LPWSTR)link.originalUrl.c_str());
}

void MainWindow::OnBatchAdd(bool onlySelected) {
    if (m_batchLinks.empty()) {
        MessageBoxW(m_hWnd, L"추가할 링크가 없습니다. 먼저 분석을 진행하세요.", L"안내", MB_OK | MB_ICONINFORMATION);
        return;
    }

    bool isSequential = (SendMessage(m_hBatchSeqCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);

    std::wstring prevTaskId = L"";
    int addedCount = 0;

    for (int i = 0; i < (int)m_batchLinks.size(); ++i) {
        if (onlySelected && !ListView_GetCheckState(m_hBatchListView, i)) {
            continue;
        }

        const auto& link = m_batchLinks[i];
        std::wstring downloadUrl = !link.directUrl.empty() ? link.directUrl : link.originalUrl;
        if (downloadUrl.empty()) continue;

        bool startNow = (!isSequential || prevTaskId.empty());
        std::wstring dependsOn = isSequential ? prevTaskId : L"";
        int parts = HttpEngine::IsSingleConnectionHost(downloadUrl, link.originalUrl) ? 1 : 0;

        auto task = DownloadManager::Instance().AddTask(
            downloadUrl,
            L"",
            link.originalUrl,
            L"",
            parts,
            startNow,
            false,
            dependsOn
        );

        if (task) {
            if (!link.filename.empty()) {
                task->SetFilename(link.filename);
            }
            prevTaskId = task->GetId();
            addedCount++;
        }
    }

    if (addedCount > 0) {
        std::wstring msg = L"총 " + std::to_wstring(addedCount) + L" 개 항목이 다운로드 목록에 추가되었습니다.";
        if (isSequential) {
            msg += L"\n\n(순차 다운로드 적용: 이전 파트가 완료되면 다음 파트가 자동으로 시작됩니다.)";
        }
        MessageBoxW(m_hWnd, msg.c_str(), L"다운로드 목록 추가 완료", MB_OK | MB_ICONINFORMATION);
        SwitchTab(0);
    } else {
        MessageBoxW(m_hWnd, L"추가할 선택된 항목이 없습니다.", L"안내", MB_OK | MB_ICONWARNING);
    }
}

void MainWindow::OnBatchClear() {
    SetWindowTextW(m_hBatchTextEdit, L"");
    m_batchLinks.clear();
    ListView_DeleteAllItems(m_hBatchListView);
    SetWindowTextW(m_hBatchStatusLbl, L"초기화되었습니다.");
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
        case ID_TASK_BATCH_EXTRACT: SwitchTab(1); break;
        case IDC_BTN_BATCH_ANALYZE: OnBatchAnalyze(); break;
        case IDC_BTN_BATCH_ADD_SELECTED: OnBatchAdd(true); break;
        case IDC_BTN_BATCH_ADD_ALL: OnBatchAdd(false); break;
        case IDC_BTN_BATCH_CLEAR: OnBatchClear(); break;
        case ID_TASK_START: OnStartSelected(); break;
        case ID_TASK_PAUSE: OnPauseSelected(); break;
        case ID_TASK_DELETE: OnDeleteSelected(); break;
        case ID_TASK_OPEN_FOLDER: OnOpenSelectedFolder(); break;
        case ID_TASK_OPEN_FILE: OnOpenSelectedFile(); break;
        case ID_TASK_COPY_URL: OnCopySelectedUrl(); break;
        case ID_TASK_SHOW_INFO: OnShowSelectedInfo(); break;
        case ID_TASK_SETTINGS: OnSettings(); break;
        case ID_EXTENSION_INSTALL: OnInstallExtension(); break;
        case ID_DPI_BYPASS_TOGGLE: OnToggleDpiBypass(); break;
        case ID_CHROME_DOH_TOGGLE: OnToggleChromeDoh(); break;
        case ID_APP_EXIT: DestroyWindow(m_hWnd); break;
        case ID_TRAY_SHOW:
            ShowWindow(m_hWnd, SW_RESTORE);
            SetForegroundWindow(m_hWnd);
            break;
        case ID_TRAY_EXIT:
            DestroyWindow(m_hWnd);
            break;
        case ID_ABOUT:
            MessageBoxW(m_hWnd, L"EasyDownload v1.4.0\n\n초고속 멀티스레드 분할 다운로드 가속기\n크롬 확장 연동 및 비디오 스트림 다운로더\nYouTube / M3U8 고화질 자동 병합 다운로더\nChrome 보안 DNS(DoH) & DPI 차단 우회 내장",
                L"EasyDownload 정보", MB_OK | MB_ICONINFORMATION);
            break;
        }
        return 0;
    }
    case WM_NOTIFY: {
        auto nm = (NMHDR*)lParam;
        if (nm->hwndFrom == m_hTabCtrl && nm->code == TCN_SELCHANGE) {
            int sel = TabCtrl_GetCurSel(m_hTabCtrl);
            SwitchTab(sel);
            return 0;
        }
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
                    AppendMenuW(hMenu, MF_STRING, ID_TASK_START, L"시작 / 재시작(&S)");
                    AppendMenuW(hMenu, MF_STRING, ID_TASK_PAUSE, L"다운로드 중단(&P)");
                    AppendMenuW(hMenu, MF_STRING, ID_TASK_DELETE, L"삭제(&D)");
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
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
    case WM_APP + 10: { // Update single batch list item
        int idx = (int)wParam;
        UpdateBatchListItem(idx);
        return 0;
    }
    case WM_APP + 11: { // Batch resolution finished
        int resolved = (int)wParam;
        int failed = (int)lParam;
        SetWindowTextW(m_hBatchBtnAnalyze, L"🔍 링크 분석 및 최종 주소 추출");
        EnableWindow(m_hBatchBtnAnalyze, TRUE);
        std::wstring status = L"분석 완료: 총 " + std::to_wstring(m_batchLinks.size()) + L" 개 중 " +
            std::to_wstring(resolved) + L" 개 추출 성공";
        if (failed > 0) {
            status += L", " + std::to_wstring(failed) + L" 개 실패";
        }
        SetWindowTextW(m_hBatchStatusLbl, status.c_str());
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
    }
    case WM_EXT_DOWNLOAD: {
        auto req = (DownloadRequest*)wParam;
        if (req) {
            // Bring client window to foreground over browser
            ForceForegroundWindow(m_hWnd);

            std::wstring fname = HttpEngine::SanitizeFilename(req->filename, 50);
            bool isYouTube = (req->url.find(L"youtube.com/") != std::wstring::npos || req->url.find(L"youtu.be/") != std::wstring::npos);
            bool isHls = (req->url.find(L".m3u8") != std::wstring::npos) || req->isVideo || isYouTube;

            if (isYouTube) {
                if (fname.empty() || fname == L"download_file" || fname == L".mp4" || HttpEngine::IsGenericFilename(fname)) {
                    fname = L"youtube_video.mp4";
                }
                if (fname.rfind(L".mp4") == std::wstring::npos) {
                    fname += L".mp4";
                }
            } else if (isHls) {
                // Video stream title resolution
                if (fname.empty() || fname == L"download_file" || fname == L".mp4" || HttpEngine::IsGenericFilename(fname)) {
                    std::wstring refTitle = HttpEngine::ExtractTitleFromRefererUrl(req->referer);
                    if (!refTitle.empty()) {
                        fname = HttpEngine::SanitizeFilename(refTitle, 50);
                    }
                }
                if (fname.empty()) {
                    fname = HttpEngine::ExtractFilenameFromUrl(req->url);
                }
                if (fname.length() >= 5 && _wcsicmp(fname.c_str() + fname.length() - 5, L".m3u8") == 0) {
                    fname = fname.substr(0, fname.length() - 5);
                }
                if (fname.rfind(L".mp4") == std::wstring::npos && fname.rfind(L".ts") == std::wstring::npos) {
                    fname += L".mp4";
                }
            } else {
                // Regular file download: NEVER use ExtractTitleFromRefererUrl (avoids _video suffix on normal files!)
                if (fname.empty() || fname == L"download_file" || HttpEngine::IsGenericFilename(fname)) {
                    fname = HttpEngine::ExtractFilenameFromUrl(req->url);
                }
                if (fname.empty()) {
                    fname = L"download.bin";
                }
            }

            std::wstring baseDir = Settings::Instance().GetTargetFolderForFilename(fname);
            WCHAR combined[MAX_PATH] = { 0 };
            PathCombineW(combined, baseDir.c_str(), fname.c_str());

            // Auto add task from Chrome with resolved category path!
            auto task = DownloadManager::Instance().AddTask(req->url, combined, req->referer, req->cookies, 8, true);
            if (task && isYouTube) {
                task->SetIsYouTube(true);
            }
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

            // Title provided directly from UI download list (max 50 chars for base title)
            std::wstring fname = HttpEngine::SanitizeFilename(info->title, 50);
            bool isHls = (info->type == L"hls") ||
                         (info->videoUrl.find(L".m3u8") != std::wstring::npos) ||
                         (info->videoUrl.find(L"playlist.txt") != std::wstring::npos) ||
                         (info->videoUrl.find(L"index.txt") != std::wstring::npos) ||
                         (info->videoUrl.find(L"master.txt") != std::wstring::npos);

            // Only fallback if title is completely empty or default placeholder
            if (fname.empty() || fname == L"download_file" || fname == L".mp4" || HttpEngine::IsGenericFilename(fname)) {
                std::wstring pageOrRef = !info->pageUrl.empty() ? info->pageUrl : info->referer;
                std::wstring refTitle = HttpEngine::ExtractTitleFromRefererUrl(pageOrRef);
                if (!refTitle.empty()) {
                    fname = HttpEngine::SanitizeFilename(refTitle, 50);
                }
            }

            if (fname.empty()) {
                fname = HttpEngine::ExtractFilenameFromUrl(info->videoUrl);
            }
            if (fname.empty()) {
                fname = L"video_stream";
            }

            // Strip .m3u8 or .txt if present
            if (fname.length() >= 5 && _wcsicmp(fname.c_str() + fname.length() - 5, L".m3u8") == 0) {
                fname = fname.substr(0, fname.length() - 5);
            } else if (fname.length() >= 4 && _wcsicmp(fname.c_str() + fname.length() - 4, L".txt") == 0) {
                fname = fname.substr(0, fname.length() - 4);
            }

            // Ensure standard playable video extension (.mp4 preferred for all video streams)
            if (fname.rfind(L".mp4") == std::wstring::npos && fname.rfind(L".ts") == std::wstring::npos &&
                fname.rfind(L".mkv") == std::wstring::npos && fname.rfind(L".webm") == std::wstring::npos) {
                fname += L".mp4";
            }

            std::wstring baseDir = Settings::Instance().GetTargetFolderForFilename(fname);
            WCHAR combined[MAX_PATH] = { 0 };
            PathCombineW(combined, baseDir.c_str(), fname.c_str());

            // Automatically start video download immediately without requiring extra dialog clicks!
            auto task = DownloadManager::Instance().AddTask(info->videoUrl, combined, info->referer, info->cookies, 8, true, isHls);
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
                bool running = DpiBypassManager::Instance().IsRunning();
                CheckMenuItem(hSub, ID_DPI_BYPASS_TOGGLE, MF_BYCOMMAND | (running ? MF_CHECKED : MF_UNCHECKED));
                bool dohOn = DpiBypassManager::IsChromeDohConfigured();
                CheckMenuItem(hSub, ID_CHROME_DOH_TOGGLE, MF_BYCOMMAND | (dohOn ? MF_CHECKED : MF_UNCHECKED));
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
        DpiBypassManager::Instance().Stop();
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(m_hWnd, uMsg, wParam, lParam);
}

} // namespace EasyDownload
