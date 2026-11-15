#include "DownloadInfoDialog.h"
#include "FormatUtils.h"
#include "../resource.h"
#include "../core/DownloadTask.h"
#include "../core/DownloadManager.h"
#include <commctrl.h>
#include <shellapi.h>
#include <map>
#include <sstream>

namespace EasyDownload {

struct InfoDialogState {
    std::shared_ptr<DownloadTask> task;
};

static std::map<std::wstring, HWND> s_activeInfoDialogs;

HWND DownloadInfoDialog::Show(HWND hParent, std::shared_ptr<DownloadTask> task) {
    if (!task) return NULL;

    auto it = s_activeInfoDialogs.find(task->GetId());
    if (it != s_activeInfoDialogs.end() && IsWindow(it->second)) {
        ForceForegroundWindow(it->second);
        return it->second;
    }

    HWND hDlg = CreateDialogParamW(
        GetModuleHandle(NULL),
        MAKEINTRESOURCEW(IDD_DOWNLOAD_INFO),
        hParent,
        DialogProc,
        (LPARAM)task.get()
    );

    if (hDlg) {
        ForceForegroundWindow(hDlg);
        UpdateWindow(hDlg);
    }

    return hDlg;
}

INT_PTR CALLBACK DownloadInfoDialog::DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG: {
        auto pTask = (DownloadTask*)lParam;
        if (!pTask) {
            DestroyWindow(hDlg);
            return TRUE;
        }

        std::shared_ptr<DownloadTask> spTask = DownloadManager::Instance().GetTask(pTask->GetId());
        if (!spTask) {
            spTask = std::shared_ptr<DownloadTask>(pTask, [](DownloadTask*) {});
        }

        auto pState = new InfoDialogState();
        pState->task = spTask;
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)pState);
        s_activeInfoDialogs[pTask->GetId()] = hDlg;

        // Set Icon
        HICON hIcon = LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDI_APP_ICON));
        if (hIcon) {
            SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        }

        // Set Controls
        SetDlgItemTextW(hDlg, IDC_LBL_INFO_FILENAME, spTask->GetFilename().c_str());

        int64_t bytes = spTask->GetDownloadedBytes() > 0 ? spTask->GetDownloadedBytes() : spTask->GetTotalBytes();
        std::wstring sizeStr = FormatBytes(bytes) + L" (" + std::to_wstring(bytes) + L" 바이트)";
        SetDlgItemTextW(hDlg, IDC_LBL_INFO_FILESIZE, sizeStr.c_str());

        std::wstring statusStr = (spTask->GetStatus() == TaskStatus::Completed) ?
            L"다운로드 완료 (정상)" : (L"상태: " + spTask->GetStatusString());
        SetDlgItemTextW(hDlg, IDC_LBL_INFO_STATUS, statusStr.c_str());

        std::wstring partsStr = L"분할 연결: " + std::to_wstring(spTask->GetNumParts()) + L"개";
        SetDlgItemTextW(hDlg, IDC_LBL_INFO_PARTS, partsStr.c_str());

        SetDlgItemTextW(hDlg, IDC_EDT_INFO_SAVEPATH, spTask->GetSavePath().c_str());
        SetDlgItemTextW(hDlg, IDC_EDT_INFO_URL, spTask->GetDisplayUrl().c_str());

        SetDlgItemTextW(hDlg, IDC_LBL_INFO_CREATED, spTask->GetCreatedAt().c_str());
        SetDlgItemTextW(hDlg, IDC_LBL_INFO_COMPLETED, spTask->GetCompletedAt().empty() ? L"-" : spTask->GetCompletedAt().c_str());

        return TRUE;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        auto pState = (InfoDialogState*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);
        std::shared_ptr<DownloadTask> task = pState ? pState->task : nullptr;

        switch (id) {
        case IDC_BTN_INFO_OPEN_FILE: {
            if (task) {
                ShellExecuteW(NULL, L"open", task->GetSavePath().c_str(), NULL, NULL, SW_SHOWNORMAL);
            }
            return TRUE;
        }
        case IDC_BTN_INFO_OPEN_FOLDER: {
            if (task) {
                std::wstring param = L"/select,\"" + task->GetSavePath() + L"\"";
                ShellExecuteW(NULL, L"open", L"explorer.exe", param.c_str(), NULL, SW_SHOWNORMAL);
            }
            return TRUE;
        }
        case IDC_BTN_INFO_COPY_URL: {
            if (task && !task->GetUrl().empty()) {
                if (OpenClipboard(hDlg)) {
                    EmptyClipboard();
                    std::wstring url = task->GetUrl();
                    size_t memSize = (url.length() + 1) * sizeof(WCHAR);
                    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, memSize);
                    if (hMem) {
                        memcpy(GlobalLock(hMem), url.c_str(), memSize);
                        GlobalUnlock(hMem);
                        SetClipboardData(CF_UNICODETEXT, hMem);
                    }
                    CloseClipboard();

                    SetDlgItemTextW(hDlg, IDC_BTN_INFO_COPY_URL, L"복사 완료!");
                    SetTimer(hDlg, 100, 1500, NULL);
                }
            }
            return TRUE;
        }
        case IDCANCEL: {
            DestroyWindow(hDlg);
            return TRUE;
        }
        }
        break;
    }

    case WM_TIMER: {
        if (wParam == 100) {
            KillTimer(hDlg, 100);
            SetDlgItemTextW(hDlg, IDC_BTN_INFO_COPY_URL, L"주소 복사(&C)");
            return TRUE;
        }
        break;
    }

    case WM_CLOSE: {
        DestroyWindow(hDlg);
        return TRUE;
    }

    case WM_DESTROY: {
        auto pState = (InfoDialogState*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);
        if (pState) {
            if (pState->task) {
                s_activeInfoDialogs.erase(pState->task->GetId());
            }
            delete pState;
            SetWindowLongPtrW(hDlg, GWLP_USERDATA, 0);
        }
        return TRUE;
    }
    }

    return FALSE;
}

} // namespace EasyDownload

