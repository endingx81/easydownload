#include "SettingsDialog.h"
#include "../resource.h"
#include "../core/Settings.h"
#include "../core/DpiBypassManager.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <shlobj.h>
#include <vector>

namespace EasyDownload {

static std::vector<CategoryRule> s_tempCategories;
static int s_currentCatIndex = 0;

static const int s_tab0Controls[] = {
    IDC_GRP_TAB0_1, IDC_EDT_DEFAULT_DIR, IDC_BTN_DIR_BROWSE,
    IDC_GRP_TAB0_2, IDC_CHK_AUTO_START, IDC_CHK_STARTUP_MINIMIZED,
    IDC_LBL_TAB0_DESC
};

static const int s_tab1Controls[] = {
    IDC_GRP_TAB1_1, IDC_LBL_TAB1_CAT, IDC_CMB_CATEGORY,
    IDC_LBL_TAB1_EXT, IDC_EDT_CATEGORY_EXTS,
    IDC_LBL_TAB1_DIR, IDC_EDT_CATEGORY_DIR, IDC_BTN_CATEGORY_BROWSE, IDC_BTN_CATEGORY_CLEAR,
    IDC_LBL_TAB1_DESC
};

static const int s_tab2Controls[] = {
    IDC_GRP_TAB2_1, IDC_LBL_TAB2_SPEED, IDC_CMB_INTERNET_SPEED,
    IDC_LBL_TAB2_INIT, IDC_EDT_INITIAL_PARTS,
    IDC_LBL_TAB2_MAX, IDC_EDT_MAX_PARTS, IDC_CHK_ADAPTIVE_SPLIT,
    IDC_LBL_TAB2_DESC
};

static const int s_tab3Controls[] = {
    IDC_GRP_TAB3_1, IDC_CHK_DPI_AUTO_START, IDC_LBL_TAB3_MODE, IDC_CMB_DPI_MODE, IDC_LBL_TAB3_DESC1,
    IDC_GRP_TAB3_2, IDC_LBL_CHROME_DOH_STATUS, IDC_BTN_CHROME_DOH_TOGGLE, IDC_LBL_TAB3_DESC2
};

static void SwitchSettingsTab(HWND hDlg, int tabIndex) {
    auto setGroupVisibility = [&](const int* ctrlIds, size_t count, int cmdShow) {
        for (size_t i = 0; i < count; ++i) {
            HWND hCtrl = GetDlgItem(hDlg, ctrlIds[i]);
            if (hCtrl) {
                ShowWindow(hCtrl, cmdShow);
            }
        }
    };

    setGroupVisibility(s_tab0Controls, _countof(s_tab0Controls), (tabIndex == 0) ? SW_SHOW : SW_HIDE);
    setGroupVisibility(s_tab1Controls, _countof(s_tab1Controls), (tabIndex == 1) ? SW_SHOW : SW_HIDE);
    setGroupVisibility(s_tab2Controls, _countof(s_tab2Controls), (tabIndex == 2) ? SW_SHOW : SW_HIDE);
    setGroupVisibility(s_tab3Controls, _countof(s_tab3Controls), (tabIndex == 3) ? SW_SHOW : SW_HIDE);

    InvalidateRect(hDlg, NULL, TRUE);
    UpdateWindow(hDlg);
}

static void SaveCurrentCategoryFields(HWND hDlg) {
    if (s_currentCatIndex >= 0 && s_currentCatIndex < (int)s_tempCategories.size()) {
        WCHAR extBuf[1024] = { 0 };
        GetDlgItemTextW(hDlg, IDC_EDT_CATEGORY_EXTS, extBuf, _countof(extBuf));
        s_tempCategories[s_currentCatIndex].extensions = extBuf;

        WCHAR dirBuf[MAX_PATH] = { 0 };
        GetDlgItemTextW(hDlg, IDC_EDT_CATEGORY_DIR, dirBuf, _countof(dirBuf));
        s_tempCategories[s_currentCatIndex].folderPath = dirBuf;
    }
}

static void LoadCategoryFields(HWND hDlg, int index) {
    if (index >= 0 && index < (int)s_tempCategories.size()) {
        s_currentCatIndex = index;
        SetDlgItemTextW(hDlg, IDC_EDT_CATEGORY_EXTS, s_tempCategories[index].extensions.c_str());
        SetDlgItemTextW(hDlg, IDC_EDT_CATEGORY_DIR, s_tempCategories[index].folderPath.c_str());
    }
}

static void UpdateChromeDohUI(HWND hDlg) {
    bool configured = DpiBypassManager::IsChromeDohConfigured();
    if (configured) {
        SetDlgItemTextW(hDlg, IDC_LBL_CHROME_DOH_STATUS, L"상태: 적용됨 (Cloudflare 1.1.1.1 DoH)");
        SetDlgItemTextW(hDlg, IDC_BTN_CHROME_DOH_TOGGLE, L"보안 DNS 해제");
    } else {
        SetDlgItemTextW(hDlg, IDC_LBL_CHROME_DOH_STATUS, L"상태: 미적용 (기본값)");
        SetDlgItemTextW(hDlg, IDC_BTN_CHROME_DOH_TOGGLE, L"보안 DNS 적용");
    }
}

void SettingsDialog::Show(HWND hParent) {
    DialogBoxW(
        GetModuleHandle(NULL),
        MAKEINTRESOURCEW(IDD_SETTINGS),
        hParent,
        DialogProc
    );
}

INT_PTR CALLBACK SettingsDialog::DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG: {
        auto& s = Settings::Instance();

        EnableThemeDialogTexture(hDlg, ETDT_ENABLETAB);

        // 1. Initialize Tabs (Tab 1 is Category Folders)
        HWND hTab = GetDlgItem(hDlg, IDC_TAB_SETTINGS);
        TCITEMW tie = { 0 };
        tie.mask = TCIF_TEXT;

        tie.pszText = (LPWSTR)L"일반 / 시작";
        TabCtrl_InsertItem(hTab, 0, &tie);

        tie.pszText = (LPWSTR)L"카테고리 폴더";
        TabCtrl_InsertItem(hTab, 1, &tie);

        tie.pszText = (LPWSTR)L"다운로드 엔진";
        TabCtrl_InsertItem(hTab, 2, &tie);

        tie.pszText = (LPWSTR)L"네트워크 / 우회";
        TabCtrl_InsertItem(hTab, 3, &tie);

        TabCtrl_SetCurSel(hTab, 0);
        SwitchSettingsTab(hDlg, 0);

        // 2. Tab 0: General & Startup
        SetDlgItemTextW(hDlg, IDC_EDT_DEFAULT_DIR, s.GetDefaultDownloadDir().c_str());
        CheckDlgButton(hDlg, IDC_CHK_AUTO_START, s.GetAutoStartWithWindows() ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHK_STARTUP_MINIMIZED, s.GetStartMinimized() ? BST_CHECKED : BST_UNCHECKED);

        // 3. Tab 1: Category Rules (2nd tab)
        s_tempCategories = s.GetCategoryRules();
        s_currentCatIndex = 0;

        HWND hCmb = GetDlgItem(hDlg, IDC_CMB_CATEGORY);
        SendMessage(hCmb, CB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < s_tempCategories.size(); ++i) {
            SendMessageW(hCmb, CB_ADDSTRING, 0, (LPARAM)s_tempCategories[i].displayName.c_str());
        }
        if (!s_tempCategories.empty()) {
            SendMessage(hCmb, CB_SETCURSEL, 0, 0);
            LoadCategoryFields(hDlg, 0);
        }

        // 4. Tab 2: Download Engine Settings (3rd tab)
        HWND hSpeedCmb = GetDlgItem(hDlg, IDC_CMB_INTERNET_SPEED);
        SendMessage(hSpeedCmb, CB_RESETCONTENT, 0, 0);
        SendMessageW(hSpeedCmb, CB_ADDSTRING, 0, (LPARAM)L"자동 / 최대 대역폭 (제한 없이 가속)");
        SendMessageW(hSpeedCmb, CB_ADDSTRING, 0, (LPARAM)L"100 Mbps (광랜 / 약 11 MB/s 도달 시 증설 중단)");
        SendMessageW(hSpeedCmb, CB_ADDSTRING, 0, (LPARAM)L"500 Mbps (기가 슬림 / 약 55 MB/s 도달 시 증설 중단)");
        SendMessageW(hSpeedCmb, CB_ADDSTRING, 0, (LPARAM)L"1 Gbps (1기가 인터넷 / 약 110 MB/s 도달 시 증설 중단)");
        SendMessageW(hSpeedCmb, CB_ADDSTRING, 0, (LPARAM)L"2.5 Gbps 이상 (초고속 인터넷 / 약 250 MB/s 도달 시 증설 중단)");
        int speedTier = s.GetInternetSpeedTier();
        if (speedTier < 0 || speedTier > 4) speedTier = 0;
        SendMessage(hSpeedCmb, CB_SETCURSEL, speedTier, 0);

        SetDlgItemInt(hDlg, IDC_EDT_INITIAL_PARTS, s.GetInitialParts(), FALSE);
        SetDlgItemInt(hDlg, IDC_EDT_MAX_PARTS, s.GetMaxParts(), FALSE);
        CheckDlgButton(hDlg, IDC_CHK_ADAPTIVE_SPLIT, s.GetAdaptiveSpeedSplitting() ? BST_CHECKED : BST_UNCHECKED);

        // 5. Tab 3: Network & Security
        CheckDlgButton(hDlg, IDC_CHK_DPI_AUTO_START, s.GetDpiBypassEnabled() ? BST_CHECKED : BST_UNCHECKED);
        HWND hDpiCmb = GetDlgItem(hDlg, IDC_CMB_DPI_MODE);
        SendMessage(hDpiCmb, CB_RESETCONTENT, 0, 0);
        SendMessageW(hDpiCmb, CB_ADDSTRING, 0, (LPARAM)L"한국 통신사 표준 (권장)");
        SendMessageW(hDpiCmb, CB_ADDSTRING, 0, (LPARAM)L"고속 모드 (-1)");
        SendMessageW(hDpiCmb, CB_ADDSTRING, 0, (LPARAM)L"강력 모드 (-9)");
        int curMode = s.GetDpiBypassMode();
        if (curMode < 0 || curMode > 2) curMode = 0;
        SendMessage(hDpiCmb, CB_SETCURSEL, curMode, 0);

        UpdateChromeDohUI(hDlg);

        return TRUE;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        SetBkMode(hdcStatic, TRANSPARENT);
        return (INT_PTR)GetStockObject(NULL_BRUSH);
    }
    case WM_NOTIFY: {
        NMHDR* pnm = (NMHDR*)lParam;
        if (pnm && pnm->idFrom == IDC_TAB_SETTINGS) {
            if (pnm->code == TCN_SELCHANGE) {
                int curSel = TabCtrl_GetCurSel(pnm->hwndFrom);
                SwitchSettingsTab(hDlg, curSel);
                return TRUE;
            }
        }
        break;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        WORD code = HIWORD(wParam);

        if (id == IDC_CMB_CATEGORY && code == CBN_SELCHANGE) {
            SaveCurrentCategoryFields(hDlg);
            int newSel = (int)SendDlgItemMessage(hDlg, IDC_CMB_CATEGORY, CB_GETCURSEL, 0, 0);
            LoadCategoryFields(hDlg, newSel);
            return TRUE;
        } else if (id == IDC_BTN_CATEGORY_BROWSE) {
            WCHAR chosenPath[MAX_PATH] = { 0 };
            BROWSEINFOW bi = { 0 };
            bi.hwndOwner = hDlg;
            bi.lpszTitle = L"이 파일 형식들이 저장될 전용 폴더를 선택하세요.";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

            LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                SHGetPathFromIDListW(pidl, chosenPath);
                CoTaskMemFree(pidl);
                SetDlgItemTextW(hDlg, IDC_EDT_CATEGORY_DIR, chosenPath);
                SaveCurrentCategoryFields(hDlg);
            }
            return TRUE;
        } else if (id == IDC_BTN_CATEGORY_CLEAR) {
            SetDlgItemTextW(hDlg, IDC_EDT_CATEGORY_DIR, L"");
            SaveCurrentCategoryFields(hDlg);
            return TRUE;
        } else if (id == IDC_BTN_DIR_BROWSE) {
            WCHAR chosenPath[MAX_PATH] = { 0 };
            BROWSEINFOW bi = { 0 };
            bi.hwndOwner = hDlg;
            bi.lpszTitle = L"다운로드 기본 저장 폴더를 선택하세요.";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

            LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                SHGetPathFromIDListW(pidl, chosenPath);
                CoTaskMemFree(pidl);
                SetDlgItemTextW(hDlg, IDC_EDT_DEFAULT_DIR, chosenPath);
            }
            return TRUE;
        } else if (id == IDC_BTN_CHROME_DOH_TOGGLE) {
            bool current = DpiBypassManager::IsChromeDohConfigured();
            bool next = !current;
            if (DpiBypassManager::SetChromeDohEnabled(next)) {
                UpdateChromeDohUI(hDlg);
                if (next) {
                    MessageBoxW(hDlg,
                        L"Chrome 보안 DNS (Cloudflare 1.1.1.1 DoH)가 성공적으로 적용되었습니다.\n\n"
                        L"통신사 차단 DNS를 암호화 우회하여 웹사이트 접속 지연 및 새로고침 문제를 해결합니다.\n\n"
                        L"* Chrome 브라우저를 완전히 닫고 다시 실행하면 즉시 적용됩니다.",
                        L"보안 DNS 적용 완료", MB_OK | MB_ICONINFORMATION);
                } else {
                    MessageBoxW(hDlg,
                        L"Chrome 보안 DNS 설정이 해제되어 기본값으로 복원되었습니다.\n\n"
                        L"* Chrome 브라우저를 완전히 닫고 다시 실행하면 원래대로 복구됩니다.",
                        L"보안 DNS 복원 완료", MB_OK | MB_ICONINFORMATION);
                }
            } else {
                MessageBoxW(hDlg, L"레지스트리 설정 변경에 실패했습니다. 관리자 권한을 확인해 주세요.", L"오류", MB_OK | MB_ICONERROR);
            }
            return TRUE;
        } else if (id == IDOK) {
            auto& s = Settings::Instance();
            WCHAR dirBuf[MAX_PATH] = { 0 };
            GetDlgItemTextW(hDlg, IDC_EDT_DEFAULT_DIR, dirBuf, MAX_PATH);
            if (wcslen(dirBuf) > 0) {
                s.SetDefaultDownloadDir(dirBuf);
            }

            // Save active category fields
            SaveCurrentCategoryFields(hDlg);
            s.SetCategoryRules(s_tempCategories);

            int speedTier = (int)SendDlgItemMessage(hDlg, IDC_CMB_INTERNET_SPEED, CB_GETCURSEL, 0, 0);
            if (speedTier < 0 || speedTier > 4) speedTier = 0;
            s.SetInternetSpeedTier(speedTier);

            int initParts = GetDlgItemInt(hDlg, IDC_EDT_INITIAL_PARTS, NULL, FALSE);
            if (initParts < 1) initParts = 1;
            if (initParts > 4) initParts = 4;
            s.SetInitialParts(initParts);

            int maxParts = GetDlgItemInt(hDlg, IDC_EDT_MAX_PARTS, NULL, FALSE);
            if (maxParts < 1) maxParts = 1;
            if (maxParts > 12) maxParts = 12;
            s.SetMaxParts(maxParts);

            s.SetAdaptiveSpeedSplitting(IsDlgButtonChecked(hDlg, IDC_CHK_ADAPTIVE_SPLIT) == BST_CHECKED);

            s.SetAutoStartWithWindows(IsDlgButtonChecked(hDlg, IDC_CHK_AUTO_START) == BST_CHECKED);
            s.SetStartMinimized(IsDlgButtonChecked(hDlg, IDC_CHK_STARTUP_MINIMIZED) == BST_CHECKED);

            bool dpiEnabled = (IsDlgButtonChecked(hDlg, IDC_CHK_DPI_AUTO_START) == BST_CHECKED);
            int dpiMode = (int)SendDlgItemMessage(hDlg, IDC_CMB_DPI_MODE, CB_GETCURSEL, 0, 0);
            if (dpiMode < 0) dpiMode = 0;
            bool dpiChanged = (s.GetDpiBypassEnabled() != dpiEnabled) || (s.GetDpiBypassMode() != dpiMode);
            s.SetDpiBypassEnabled(dpiEnabled);
            s.SetDpiBypassMode(dpiMode);

            s.Save();

            if (dpiChanged) {
                if (dpiEnabled) {
                    DpiBypassManager::Instance().Start(dpiMode);
                } else {
                    DpiBypassManager::Instance().Stop();
                }
            }

            EndDialog(hDlg, IDOK);
            return TRUE;
        } else if (id == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

} // namespace EasyDownload
