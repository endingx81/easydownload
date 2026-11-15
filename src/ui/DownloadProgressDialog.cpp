#include "DownloadProgressDialog.h"
#include "SegmentBar.h"
#include "FormatUtils.h"
#include "../resource.h"
#include "../core/DownloadTask.h"
#include "../core/DownloadManager.h"
#include <commctrl.h>
#include <shellapi.h>
#include <map>
#include <sstream>
#include <iomanip>

namespace EasyDownload {

struct DialogState {
    std::shared_ptr<DownloadTask> task;
    HWND hSegmentBar{ NULL };
    HWND hProgressBar{ NULL };
    bool wasActiveOnOpen{ false };
    bool autoClosed{ false };
};

static std::map<std::wstring, HWND> s_activeDialogs;
static bool s_autoCloseOnComplete = false;

HWND DownloadProgressDialog::Show(HWND hParent, std::shared_ptr<DownloadTask> task) {
    if (!task) return NULL;

    auto it = s_activeDialogs.find(task->GetId());
    if (it != s_activeDialogs.end() && IsWindow(it->second)) {
        ForceForegroundWindow(it->second);
        return it->second;
    }

    HWND hDlg = CreateDialogParamW(
        GetModuleHandle(NULL),
        MAKEINTRESOURCEW(IDD_DOWNLOAD_PROGRESS),
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

void DownloadProgressDialog::CloseAll() {
    for (auto& pair : s_activeDialogs) {
        if (IsWindow(pair.second)) {
            DestroyWindow(pair.second);
        }
    }
    s_activeDialogs.clear();
}

INT_PTR CALLBACK DownloadProgressDialog::DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG: {
        auto pTask = (DownloadTask*)lParam;
        if (!pTask) {
            DestroyWindow(hDlg);
            return TRUE;
        }

        // Keep shared_ptr to task
        std::shared_ptr<DownloadTask> spTask = DownloadManager::Instance().GetTask(pTask->GetId());
        if (!spTask) {
            spTask = std::shared_ptr<DownloadTask>(pTask, [](DownloadTask*) {});
        }

        DialogState* pState = new DialogState();
        pState->task = spTask;
        pState->wasActiveOnOpen = (pTask->GetStatus() == TaskStatus::Downloading ||
                                   pTask->GetStatus() == TaskStatus::Connecting);

        // Set Icon
        HICON hIcon = LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDI_APP_ICON));
        if (hIcon) {
            SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        }

        // Set Labels
        SetDlgItemTextW(hDlg, IDC_LBL_PROG_FILENAME, pTask->GetFilename().c_str());
        SetDlgItemTextW(hDlg, IDC_LBL_PROG_SAVETO, pTask->GetSavePath().c_str());
        SetDlgItemTextW(hDlg, IDC_LBL_PROG_URL, pTask->GetDisplayUrl().c_str());

        // Initialize Progress Bar
        HWND hPrg = GetDlgItem(hDlg, IDC_PRG_PROGRESS);
        pState->hProgressBar = hPrg;
        SendMessage(hPrg, PBM_SETRANGE32, 0, 1000);
        SendMessage(hPrg, PBM_SETPOS, 0, 0);

        // Position and create custom SegmentBar
        HWND hContainer = GetDlgItem(hDlg, IDC_SEGMENT_CONTAINER);
        RECT rc = { 0 };
        GetWindowRect(hContainer, &rc);
        MapWindowPoints(NULL, hDlg, (LPPOINT)&rc, 2);

        HWND hSeg = SegmentBar::Create(
            hDlg,
            rc.left, rc.top,
            rc.right - rc.left, rc.bottom - rc.top,
            IDC_SEGMENT_BAR
        );
        pState->hSegmentBar = hSeg;
        if (hSeg && IsWindow(hSeg)) {
            SegmentBar::SetChunks(hSeg, pTask->GetChunksProgress(), pTask->GetTotalBytes());
        }

        // Store into Dialog UserData immediately
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)pState);
        s_activeDialogs[pTask->GetId()] = hDlg;

        // Initially disable Open buttons
        EnableWindow(GetDlgItem(hDlg, IDC_BTN_PROG_OPEN_FILE), FALSE);
        EnableWindow(GetDlgItem(hDlg, IDC_BTN_PROG_OPEN_FOLDER), FALSE);

        // Checkbox: Close dialog on completion
        CheckDlgButton(hDlg, IDC_CHK_CLOSE_WHEN_DONE, s_autoCloseOnComplete ? BST_CHECKED : BST_UNCHECKED);

        // Update timer (100ms)
        SetTimer(hDlg, 1, 100, NULL);
        return TRUE;
    }

    case WM_TIMER: {
        if (wParam == 1) {
            auto pState = (DialogState*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);
            if (!pState || !pState->task) return 0;

            auto task = pState->task;
            int64_t total = task->GetTotalBytes();
            int64_t down = task->GetDownloadedBytes();
            int64_t speed = task->GetCurrentSpeed();
            TaskStatus status = task->GetStatus();

            // Progress bar
            HWND hPrg = pState->hProgressBar ? pState->hProgressBar : GetDlgItem(hDlg, IDC_PRG_PROGRESS);
            if (total > 0) {
                int pos = (int)((double)down / (double)total * 1000.0);
                if (pos > 1000) pos = 1000;
                SendMessage(hPrg, PBM_SETPOS, pos, 0);
            } else if (status == TaskStatus::Completed) {
                SendMessage(hPrg, PBM_SETPOS, 1000, 0);
            } else if (down > 0) {
                // If total is unknown, cycle progress
                int cyclePos = (int)((down / 1024) % 1000);
                SendMessage(hPrg, PBM_SETPOS, cyclePos, 0);
            }

            // Segment Bar
            HWND hSeg = pState->hSegmentBar ? pState->hSegmentBar : GetDlgItem(hDlg, IDC_SEGMENT_BAR);
            if (hSeg && IsWindow(hSeg)) {
                SegmentBar::SetChunks(hSeg, task->GetChunksProgress(), total);
            }

            // Status label & Parts
            std::wstring statusText = L"상태: " + task->GetStatusString();
            SetDlgItemTextW(hDlg, IDC_LBL_PROG_STATUS, statusText.c_str());

            int active = task->GetActiveWorkersCount();
            std::wstring concurrentText;
            if (status == TaskStatus::Downloading) {
                concurrentText = L"분할 연결: " + std::to_wstring(active > 0 ? active : 1) + L"개 (최대 12개)";
            } else if (status == TaskStatus::Connecting) {
                concurrentText = L"분할 연결: 연결 중";
            } else if (status == TaskStatus::Completed) {
                concurrentText = L"분할 연결: 완료";
            } else if (status == TaskStatus::Paused) {
                concurrentText = L"분할 연결: 중단됨";
            } else {
                concurrentText = L"분할 연결: -";
            }
            SetDlgItemTextW(hDlg, IDC_LBL_PROG_PARTS, concurrentText.c_str());

            // Size label
            std::wstring sizeText;
            if (total > 0) {
                double pct = (double)down / (double)total * 100.0;
                if (pct > 100.0) pct = 100.0;
                std::wstringstream ss;
                ss << L"크기: " << FormatBytes(down) << L" / " << FormatBytes(total)
                   << L" (" << std::fixed << std::setprecision(1) << pct << L"%)";
                sizeText = ss.str();
            } else if (down > 0) {
                sizeText = L"크기: " + FormatBytes(down);
            } else {
                sizeText = L"크기: 0 B";
            }
            SetDlgItemTextW(hDlg, IDC_LBL_PROG_SIZE, sizeText.c_str());

            // Speed & ETA
            if (status == TaskStatus::Downloading) {
                SetDlgItemTextW(hDlg, IDC_LBL_PROG_SPEED, (L"전송 속도: " + FormatSpeed(speed)).c_str());
                SetDlgItemTextW(hDlg, IDC_LBL_PROG_TIME, (L"남은 시간: " + FormatEta(task->GetRemainingSeconds())).c_str());
                SetDlgItemTextW(hDlg, IDC_BTN_PROG_PAUSE, L"중단");
                ShowWindow(GetDlgItem(hDlg, IDC_BTN_PROG_PAUSE), SW_SHOW);
                EnableWindow(GetDlgItem(hDlg, IDC_BTN_PROG_PAUSE), TRUE);
                SetDlgItemTextW(hDlg, IDC_BTN_PROG_CANCEL, L"닫기");
            } else if (status == TaskStatus::Connecting) {
                SetDlgItemTextW(hDlg, IDC_LBL_PROG_SPEED, L"연결 중...");
                SetDlgItemTextW(hDlg, IDC_LBL_PROG_TIME, L"대기 중");
                SetDlgItemTextW(hDlg, IDC_BTN_PROG_PAUSE, L"중단");
                ShowWindow(GetDlgItem(hDlg, IDC_BTN_PROG_PAUSE), SW_SHOW);
                EnableWindow(GetDlgItem(hDlg, IDC_BTN_PROG_PAUSE), TRUE);
                SetDlgItemTextW(hDlg, IDC_BTN_PROG_CANCEL, L"닫기");
            } else if (status == TaskStatus::Paused) {
                SetDlgItemTextW(hDlg, IDC_LBL_PROG_SPEED, L"전송 속도: -");
                SetDlgItemTextW(hDlg, IDC_LBL_PROG_TIME, L"남은 시간: 중단됨");
                SetDlgItemTextW(hDlg, IDC_BTN_PROG_PAUSE, L"재시작");
                ShowWindow(GetDlgItem(hDlg, IDC_BTN_PROG_PAUSE), SW_SHOW);
                EnableWindow(GetDlgItem(hDlg, IDC_BTN_PROG_PAUSE), TRUE);
                SetDlgItemTextW(hDlg, IDC_BTN_PROG_CANCEL, L"닫기");
            } else if (status == TaskStatus::Completed) {
                SetDlgItemTextW(hDlg, IDC_LBL_PROG_SPEED, L"전송 완료");
                SetDlgItemTextW(hDlg, IDC_LBL_PROG_TIME, L"완료됨");
                ShowWindow(GetDlgItem(hDlg, IDC_BTN_PROG_PAUSE), SW_HIDE);

                // Show 닫기 button on the right!
                SetDlgItemTextW(hDlg, IDC_BTN_PROG_CANCEL, L"닫기");
                EnableWindow(GetDlgItem(hDlg, IDC_BTN_PROG_CANCEL), TRUE);

                EnableWindow(GetDlgItem(hDlg, IDC_BTN_PROG_OPEN_FILE), TRUE);
                EnableWindow(GetDlgItem(hDlg, IDC_BTN_PROG_OPEN_FOLDER), TRUE);

                // Auto close dialog ONLY IF it was actively downloading when opened and just finished!
                if (pState->wasActiveOnOpen && !pState->autoClosed) {
                    pState->autoClosed = true;
                    if (IsDlgButtonChecked(hDlg, IDC_CHK_CLOSE_WHEN_DONE) == BST_CHECKED) {
                        DestroyWindow(hDlg);
                        return TRUE;
                    }
                }
            } else if (status == TaskStatus::Error) {
                SetDlgItemTextW(hDlg, IDC_LBL_PROG_SPEED, L"오류 발생");
                SetDlgItemTextW(hDlg, IDC_LBL_PROG_TIME, task->GetErrorMessage().c_str());
                SetDlgItemTextW(hDlg, IDC_BTN_PROG_PAUSE, L"다시 시도");
                ShowWindow(GetDlgItem(hDlg, IDC_BTN_PROG_PAUSE), SW_SHOW);
                EnableWindow(GetDlgItem(hDlg, IDC_BTN_PROG_PAUSE), TRUE);
                SetDlgItemTextW(hDlg, IDC_BTN_PROG_CANCEL, L"닫기");
            }
        }
        return TRUE;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        auto pState = (DialogState*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);
        std::shared_ptr<DownloadTask> task = pState ? pState->task : nullptr;

        switch (id) {
        case IDC_CHK_CLOSE_WHEN_DONE: {
            s_autoCloseOnComplete = (IsDlgButtonChecked(hDlg, IDC_CHK_CLOSE_WHEN_DONE) == BST_CHECKED);
            return TRUE;
        }
        case IDC_BTN_PROG_PAUSE: {
            if (task) {
                if (task->GetStatus() == TaskStatus::Downloading || task->GetStatus() == TaskStatus::Connecting) {
                    task->Pause();
                } else if (task->GetStatus() == TaskStatus::Paused || task->GetStatus() == TaskStatus::Error) {
                    task->Resume();
                }
            }
            return TRUE;
        }
        case IDC_BTN_PROG_CANCEL:
        case IDC_BTN_PROG_HIDE:
        case IDCANCEL: {
            // Close progress dialog window; download continues in main dashboard
            DestroyWindow(hDlg);
            return TRUE;
        }
        case IDC_BTN_PROG_OPEN_FILE: {
            if (task) {
                ShellExecuteW(NULL, L"open", task->GetSavePath().c_str(), NULL, NULL, SW_SHOWNORMAL);
            }
            return TRUE;
        }
        case IDC_BTN_PROG_OPEN_FOLDER: {
            if (task) {
                std::wstring param = L"/select,\"" + task->GetSavePath() + L"\"";
                ShellExecuteW(NULL, L"open", L"explorer.exe", param.c_str(), NULL, SW_SHOWNORMAL);
            }
            return TRUE;
        }
        }
        break;
    }

    case WM_CLOSE: {
        DestroyWindow(hDlg);
        return TRUE;
    }

    case WM_DESTROY: {
        KillTimer(hDlg, 1);
        auto pState = (DialogState*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);
        if (pState) {
            if (pState->task) {
                s_activeDialogs.erase(pState->task->GetId());
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
