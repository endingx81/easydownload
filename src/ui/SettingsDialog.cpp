#include "SettingsDialog.h"
#include "../resource.h"
#include "../core/Settings.h"
#include <shlobj.h>
#include <vector>

namespace EasyDownload {

static std::vector<CategoryRule> s_tempCategories;
static int s_currentCatIndex = 0;

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
        SetDlgItemTextW(hDlg, IDC_EDT_DEFAULT_DIR, s.GetDefaultDownloadDir().c_str());
        SetDlgItemInt(hDlg, IDC_EDT_INITIAL_PARTS, s.GetInitialParts(), FALSE);
        SetDlgItemInt(hDlg, IDC_EDT_MAX_PARTS, s.GetMaxParts(), FALSE);
        CheckDlgButton(hDlg, IDC_CHK_ADAPTIVE_SPLIT, s.GetAdaptiveSpeedSplitting() ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemInt(hDlg, IDC_EDT_MAX_TASKS, s.GetMaxConcurrentTasks(), FALSE);

        CheckDlgButton(hDlg, IDC_CHK_AUTO_START, s.GetAutoStartWithWindows() ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHK_STARTUP_MINIMIZED, s.GetStartMinimized() ? BST_CHECKED : BST_UNCHECKED);

        // Load Categories
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

        return TRUE;
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

            int initParts = GetDlgItemInt(hDlg, IDC_EDT_INITIAL_PARTS, NULL, FALSE);
            if (initParts < 1) initParts = 1;
            if (initParts > 4) initParts = 4;
            s.SetInitialParts(initParts);

            int maxParts = GetDlgItemInt(hDlg, IDC_EDT_MAX_PARTS, NULL, FALSE);
            if (maxParts < 1) maxParts = 1;
            if (maxParts > 12) maxParts = 12;
            s.SetMaxParts(maxParts);

            s.SetAdaptiveSpeedSplitting(IsDlgButtonChecked(hDlg, IDC_CHK_ADAPTIVE_SPLIT) == BST_CHECKED);

            int tasks = GetDlgItemInt(hDlg, IDC_EDT_MAX_TASKS, NULL, FALSE);
            if (tasks < 1) tasks = 1;
            if (tasks > 16) tasks = 16;
            s.SetMaxConcurrentTasks(tasks);

            s.SetAutoStartWithWindows(IsDlgButtonChecked(hDlg, IDC_CHK_AUTO_START) == BST_CHECKED);
            s.SetStartMinimized(IsDlgButtonChecked(hDlg, IDC_CHK_STARTUP_MINIMIZED) == BST_CHECKED);

            s.Save();
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
