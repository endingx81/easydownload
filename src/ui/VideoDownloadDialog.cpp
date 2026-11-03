#include "VideoDownloadDialog.h"
#include "FormatUtils.h"
#include "../resource.h"
#include "../core/HlsDownloader.h"
#include "../core/HttpEngine.h"
#include "../core/Settings.h"
#include "../core/DownloadManager.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <thread>
#include <atomic>

namespace EasyDownload {

struct VideoDlgContext {
    std::wstring initUrl;
    std::wstring initTitle;
    std::wstring referer;
    std::wstring cookies;
    std::wstring selectedUrl;
    std::wstring savePath;
    HlsPlaylistInfo playlistInfo;
};

static VideoDlgContext* s_pVideoCtx = nullptr;

bool VideoDownloadDialog::Show(
    HWND hParent,
    std::wstring& outStreamUrl,
    std::wstring& outSavePath,
    const std::wstring& initUrl,
    const std::wstring& initTitle,
    const std::wstring& referer,
    const std::wstring& cookies
) {
    VideoDlgContext ctx;
    ctx.initUrl = initUrl;
    ctx.initTitle = initTitle;
    ctx.referer = referer;
    ctx.cookies = cookies;

    s_pVideoCtx = &ctx;

    INT_PTR res = DialogBoxParamW(
        GetModuleHandle(NULL),
        MAKEINTRESOURCEW(IDD_NEW_VIDEO),
        hParent,
        DialogProc,
        (LPARAM)&ctx
    );

    s_pVideoCtx = nullptr;

    if (res == IDOK) {
        outStreamUrl = ctx.selectedUrl;
        outSavePath = ctx.savePath;
        return true;
    }
    return false;
}

static void AnalyzeStream(HWND hDlg) {
    WCHAR urlBuf[2048] = { 0 };
    GetDlgItemTextW(hDlg, IDC_EDT_VIDEO_URL, urlBuf, 2048);
    std::wstring url = urlBuf;

    if (url.empty()) {
        MessageBoxW(hDlg, L"비디오 또는 M3U8 스트림 URL을 입력하세요.", L"알림", MB_OK | MB_ICONWARNING);
        return;
    }

    SetDlgItemTextW(hDlg, IDC_LBL_VIDEO_STATUS, L"스트림 분석 및 대역폭/용량 계산 중...");
    EnableWindow(GetDlgItem(hDlg, IDC_BTN_ANALYZE), FALSE);

    HWND hList = GetDlgItem(hDlg, IDC_LST_STREAMS);
    ListView_DeleteAllItems(hList);

    std::wstring referer = s_pVideoCtx ? s_pVideoCtx->referer : L"";
    std::wstring cookies = s_pVideoCtx ? s_pVideoCtx->cookies : L"";

    std::thread([hDlg, hList, url, referer, cookies]() {
        std::atomic<bool> abortFlag{ false };
        HlsPlaylistInfo info;
        bool ok = HlsDownloader::ParsePlaylist(url, referer, cookies, info, abortFlag);

        // If sub-quality like index-f3...m3u8, probe parent master (master.m3u8 or index.m3u8) for full quality choices
        if (ok && !info.isMaster && url.find(L"-f") != std::wstring::npos) {
            size_t fPos = url.find(L"-f");
            size_t slashPos = url.rfind(L'/', fPos);
            if (slashPos != std::wstring::npos) {
                std::vector<std::wstring> candidates = {
                    url.substr(0, slashPos + 1) + L"master.m3u8",
                    url.substr(0, slashPos + 1) + L"index.m3u8",
                    url.substr(0, slashPos + 1) + L"playlist.m3u8"
                };
                for (const auto& cUrl : candidates) {
                    HlsPlaylistInfo masterInfo;
                    if (HlsDownloader::ParsePlaylist(cUrl, referer, cookies, masterInfo, abortFlag) && masterInfo.isMaster) {
                        info = masterInfo;
                        break;
                    }
                }
            }
        }

        PostMessage(hDlg, WM_APP + 101, (WPARAM)ok, 0);

        if (ok && s_pVideoCtx) {
            s_pVideoCtx->playlistInfo = info;
        }
    }).detach();
}

INT_PTR CALLBACK VideoDownloadDialog::DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG: {
        auto ctx = (VideoDlgContext*)lParam;
        SetWindowLongPtrW(hDlg, DWLP_USER, (LONG_PTR)ctx);

        ForceForegroundWindow(hDlg);

        HWND hList = GetDlgItem(hDlg, IDC_LST_STREAMS);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

        LVCOLUMNW col = { 0 };
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

        col.pszText = (LPWSTR)L"품질 / 해상도";
        col.cx = 110;
        ListView_InsertColumn(hList, 0, &col);

        col.pszText = (LPWSTR)L"예상 파일 크기";
        col.cx = 105;
        ListView_InsertColumn(hList, 1, &col);

        col.pszText = (LPWSTR)L"대역폭 (비트레이트)";
        col.cx = 120;
        ListView_InsertColumn(hList, 2, &col);

        col.pszText = (LPWSTR)L"스트림 URL";
        col.cx = 220;
        ListView_InsertColumn(hList, 3, &col);

        if (!ctx->initUrl.empty()) {
            SetDlgItemTextW(hDlg, IDC_EDT_VIDEO_URL, ctx->initUrl.c_str());
        }
        std::wstring title = ctx->initTitle.empty() ? L"video_stream.mp4" : ctx->initTitle;
        if (title.rfind(L".mp4") == std::wstring::npos && title.rfind(L".ts") == std::wstring::npos) {
            title += L".mp4";
        }
        SetDlgItemTextW(hDlg, IDC_EDT_VIDEO_TITLE, title.c_str());

        // Category-based folder resolution
        std::wstring dir = Settings::Instance().GetTargetFolderForFilename(title);
        WCHAR combined[MAX_PATH] = { 0 };
        PathCombineW(combined, dir.c_str(), title.c_str());
        std::wstring uniquePath = DownloadManager::Instance().GenerateUniqueSavePath(combined);
        SetDlgItemTextW(hDlg, IDC_EDT_VIDEO_SAVEPATH, uniquePath.c_str());

        if (!ctx->initUrl.empty()) {
            AnalyzeStream(hDlg);
        }
        return TRUE;
    }
    case WM_APP + 101: {
        bool ok = (wParam != 0);
        EnableWindow(GetDlgItem(hDlg, IDC_BTN_ANALYZE), TRUE);
        HWND hList = GetDlgItem(hDlg, IDC_LST_STREAMS);

        if (ok && s_pVideoCtx) {
            SetDlgItemTextW(hDlg, IDC_LBL_VIDEO_STATUS, L"스트림 분석 완료! 원하는 화질을 선택하세요.");

            if (s_pVideoCtx->playlistInfo.isMaster && !s_pVideoCtx->playlistInfo.qualities.empty()) {
                int row = 0;
                for (const auto& q : s_pVideoCtx->playlistInfo.qualities) {
                    LVITEMW item = { 0 };
                    item.mask = LVIF_TEXT;
                    item.iItem = row;
                    item.pszText = (LPWSTR)q.resolution.c_str();
                    ListView_InsertItem(hList, &item);

                    std::wstring sizeStr = (q.estimatedBytes > 0) ? (L"~" + FormatBytes(q.estimatedBytes)) : L"스트리밍";
                    ListView_SetItemText(hList, row, 1, (LPWSTR)sizeStr.c_str());

                    std::wstring bwStr = std::to_wstring(q.bandwidth / 1000) + L" kbps (" + FormatSpeed(q.bandwidth / 8) + L")";
                    ListView_SetItemText(hList, row, 2, (LPWSTR)bwStr.c_str());
                    ListView_SetItemText(hList, row, 3, (LPWSTR)q.url.c_str());
                    row++;
                }
                // Auto select highest quality (last item or first item with highest bandwidth)
                int bestRow = row - 1;
                int maxBw = 0;
                for (int r = 0; r < (int)s_pVideoCtx->playlistInfo.qualities.size(); ++r) {
                    if (s_pVideoCtx->playlistInfo.qualities[r].bandwidth > maxBw) {
                        maxBw = s_pVideoCtx->playlistInfo.qualities[r].bandwidth;
                        bestRow = r;
                    }
                }
                ListView_SetItemState(hList, bestRow, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            } else {
                // Single stream or direct segment list
                LVITEMW item = { 0 };
                item.mask = LVIF_TEXT;
                item.iItem = 0;
                item.pszText = (LPWSTR)L"기본 스트림 (단일 품질)";
                ListView_InsertItem(hList, &item);

                std::wstring segStr = std::to_wstring(s_pVideoCtx->playlistInfo.segmentUrls.size()) + L" 개 세그먼트";
                ListView_SetItemText(hList, 0, 1, (LPWSTR)segStr.c_str());
                ListView_SetItemText(hList, 0, 2, (LPWSTR)L"직접 스트림");

                WCHAR urlBuf[2048] = { 0 };
                GetDlgItemTextW(hDlg, IDC_EDT_VIDEO_URL, urlBuf, 2048);
                ListView_SetItemText(hList, 0, 3, urlBuf);

                ListView_SetItemState(hList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
        } else {
            SetDlgItemTextW(hDlg, IDC_LBL_VIDEO_STATUS, L"M3U8 마스터 분석 불가: 원본 스트림으로 직접 다운로드합니다.");
            LVITEMW item = { 0 };
            item.mask = LVIF_TEXT;
            item.iItem = 0;
            item.pszText = (LPWSTR)L"원본 스트림";
            ListView_InsertItem(hList, &item);

            WCHAR urlBuf[2048] = { 0 };
            GetDlgItemTextW(hDlg, IDC_EDT_VIDEO_URL, urlBuf, 2048);
            ListView_SetItemText(hList, 0, 1, (LPWSTR)L"직접 수신");
            ListView_SetItemText(hList, 0, 2, (LPWSTR)L"원본");
            ListView_SetItemText(hList, 0, 3, urlBuf);
            ListView_SetItemState(hList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
        return TRUE;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        WORD code = HIWORD(wParam);

        if (id == IDC_BTN_ANALYZE) {
            AnalyzeStream(hDlg);
            return TRUE;
        } else if (id == IDC_EDT_VIDEO_TITLE && code == EN_KILLFOCUS) {
            WCHAR titleBuf[MAX_PATH] = { 0 };
            GetDlgItemTextW(hDlg, IDC_EDT_VIDEO_TITLE, titleBuf, MAX_PATH);
            std::wstring curTitle = HttpEngine::SanitizeFilename(titleBuf);
            if (curTitle.empty()) curTitle = L"video_stream.mp4";
            if (curTitle.rfind(L".mp4") == std::wstring::npos && curTitle.rfind(L".ts") == std::wstring::npos) {
                curTitle += L".mp4";
            }

            WCHAR currentSavePath[MAX_PATH] = { 0 };
            GetDlgItemTextW(hDlg, IDC_EDT_VIDEO_SAVEPATH, currentSavePath, MAX_PATH);
            WCHAR dirOnly[MAX_PATH] = { 0 };
            wcscpy_s(dirOnly, currentSavePath);
            PathRemoveFileSpecW(dirOnly);

            std::wstring baseDir = (wcslen(dirOnly) > 0) ? dirOnly : Settings::Instance().GetTargetFolderForFilename(curTitle);
            WCHAR combined[MAX_PATH] = { 0 };
            PathCombineW(combined, baseDir.c_str(), curTitle.c_str());
            std::wstring uniquePath = DownloadManager::Instance().GenerateUniqueSavePath(combined);
            SetDlgItemTextW(hDlg, IDC_EDT_VIDEO_SAVEPATH, uniquePath.c_str());
            return TRUE;
        } else if (id == IDC_BTN_VIDEO_BROWSE) {
            WCHAR currentSavePath[MAX_PATH] = { 0 };
            GetDlgItemTextW(hDlg, IDC_EDT_VIDEO_SAVEPATH, currentSavePath, MAX_PATH);

            OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
            ofn.hwndOwner = hDlg;
            ofn.lpstrFile = currentSavePath;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"동영상 파일 (*.mp4;*.ts;*.mkv)\0*.mp4;*.ts;*.mkv\0모든 파일 (*.*)\0*.*\0";
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

            if (GetSaveFileNameW(&ofn)) {
                SetDlgItemTextW(hDlg, IDC_EDT_VIDEO_SAVEPATH, currentSavePath);
                WCHAR fname[MAX_PATH] = { 0 };
                wcscpy_s(fname, currentSavePath);
                PathStripPathW(fname);
                if (wcslen(fname) > 0) {
                    SetDlgItemTextW(hDlg, IDC_EDT_VIDEO_TITLE, fname);
                }
            }
            return TRUE;
        } else if (id == IDOK) {
            auto ctx = (VideoDlgContext*)GetWindowLongPtrW(hDlg, DWLP_USER);
            if (ctx) {
                HWND hList = GetDlgItem(hDlg, IDC_LST_STREAMS);
                int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
                if (sel >= 0) {
                    WCHAR urlBuf[2048] = { 0 };
                    ListView_GetItemText(hList, sel, 3, urlBuf, 2048);
                    ctx->selectedUrl = urlBuf;
                } else {
                    WCHAR urlBuf[2048] = { 0 };
                    GetDlgItemTextW(hDlg, IDC_EDT_VIDEO_URL, urlBuf, 2048);
                    ctx->selectedUrl = urlBuf;
                }

                if (ctx->selectedUrl.empty()) {
                    MessageBoxW(hDlg, L"다운로드할 스트림을 선택하세요.", L"알림", MB_OK | MB_ICONWARNING);
                    return TRUE;
                }

                WCHAR saveBuf[MAX_PATH] = { 0 };
                GetDlgItemTextW(hDlg, IDC_EDT_VIDEO_SAVEPATH, saveBuf, MAX_PATH);
                std::wstring finalPath = saveBuf;

                if (finalPath.empty()) {
                    WCHAR titleBuf[MAX_PATH] = { 0 };
                    GetDlgItemTextW(hDlg, IDC_EDT_VIDEO_TITLE, titleBuf, MAX_PATH);
                    std::wstring title = HttpEngine::SanitizeFilename(titleBuf);
                    if (title.empty()) title = L"video_stream.mp4";
                    if (title.rfind(L".mp4") == std::wstring::npos && title.rfind(L".ts") == std::wstring::npos) {
                        title += L".mp4";
                    }

                    std::wstring dir = Settings::Instance().GetTargetFolderForFilename(title);
                    WCHAR combined[MAX_PATH] = { 0 };
                    PathCombineW(combined, dir.c_str(), title.c_str());
                    finalPath = combined;
                }

                ctx->savePath = finalPath;
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
