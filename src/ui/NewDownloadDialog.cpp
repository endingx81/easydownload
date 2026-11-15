#include "NewDownloadDialog.h"
#include "FormatUtils.h"
#include "../resource.h"
#include "../core/Settings.h"
#include "../core/HttpEngine.h"
#include "../core/DownloadManager.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shlwapi.h>

namespace EasyDownload {

struct NewDlgContext {
    std::wstring url;
    std::wstring savePath;
    int parts{ 8 };
    bool startImmediate{ true };
    std::wstring initUrl;
    std::wstring initFilename;
};

static NewDlgContext* s_pContext = nullptr;

bool NewDownloadDialog::Show(
    HWND hParent,
    std::wstring& outUrl,
    std::wstring& outSavePath,
    int& outParts,
    bool& outStartImmediate,
    const std::wstring& initUrl,
    const std::wstring& initFilename
) {
    NewDlgContext ctx;
    ctx.parts = Settings::Instance().GetInitialParts();
    ctx.startImmediate = true;
    ctx.initUrl = initUrl;
    ctx.initFilename = initFilename;

    s_pContext = &ctx;

    INT_PTR res = DialogBoxParamW(
        GetModuleHandle(NULL),
        MAKEINTRESOURCEW(IDD_NEW_DOWNLOAD),
        hParent,
        DialogProc,
        (LPARAM)&ctx
    );

    s_pContext = nullptr;

    if (res == IDOK) {
        outUrl = ctx.url;
        outSavePath = ctx.savePath;
        outParts = ctx.parts;
        outStartImmediate = ctx.startImmediate;
        return true;
    }
    return false;
}

static void UpdateSavePathFromUrl(HWND hDlg) {
    WCHAR urlBuf[2048] = { 0 };
    GetDlgItemTextW(hDlg, IDC_EDT_URL, urlBuf, 2048);
    std::wstring url = urlBuf;

    if (!url.empty()) {
        std::wstring fname = HttpEngine::ExtractFilenameFromUrl(url);
        bool isYouTube = (url.find(L"youtube.com/") != std::wstring::npos || url.find(L"youtu.be/") != std::wstring::npos);
        if (isYouTube) {
            fname = L"youtube_video.mp4";
        }
        bool isSextb = (url.find(L"sextb.net/") != std::wstring::npos);
        if (isSextb) {
            std::wstring clean = url;
            size_t q = clean.find(L'?');
            if (q != std::wstring::npos) clean = clean.substr(0, q);
            while (!clean.empty() && (clean.back() == L'/' || clean.back() == L' ')) clean.pop_back();
            size_t slash = clean.find_last_of(L'/');
            std::wstring slug = (slash != std::wstring::npos) ? clean.substr(slash + 1) : L"sextb_video";
            fname = slug + L".mp4";
        }
        bool isAv01 = (url.find(L"av01.media") != std::wstring::npos || url.find(L"av01.tv") != std::wstring::npos);
        if (isAv01) {
            std::wstring clean = url;
            size_t q = clean.find(L'?');
            if (q != std::wstring::npos) clean = clean.substr(0, q);
            while (!clean.empty() && (clean.back() == L'/' || clean.back() == L' ')) clean.pop_back();
            size_t slash = clean.find_last_of(L'/');
            std::wstring slug = (slash != std::wstring::npos) ? clean.substr(slash + 1) : L"av01_video";
            fname = slug + L".mp4";
        }
        bool isHls = (url.find(L".m3u8") != std::wstring::npos) ||
                     (url.find(L"playlist.txt") != std::wstring::npos) ||
                     (url.find(L"index.txt") != std::wstring::npos) ||
                     (url.find(L"master.txt") != std::wstring::npos);
        if (isHls && fname.length() >= 5 && _wcsicmp(fname.c_str() + fname.length() - 5, L".m3u8") == 0) {
            fname = fname.substr(0, fname.length() - 5) + L".mp4";
        } else if (isHls && fname.length() >= 4 && _wcsicmp(fname.c_str() + fname.length() - 4, L".txt") == 0) {
            fname = fname.substr(0, fname.length() - 4) + L".mp4";
        }
        std::wstring dir = Settings::Instance().GetTargetFolderForFilename(fname);
        WCHAR combined[MAX_PATH] = { 0 };
        PathCombineW(combined, dir.c_str(), fname.c_str());
        std::wstring uniquePath = DownloadManager::Instance().GenerateUniqueSavePath(combined);
        SetDlgItemTextW(hDlg, IDC_EDT_SAVE_PATH, uniquePath.c_str());

        if (HttpEngine::IsSingleConnectionHost(url)) {
            SetDlgItemInt(hDlg, IDC_EDT_PARTS, 1, FALSE);
            SendDlgItemMessage(hDlg, IDC_SPN_PARTS, UDM_SETPOS32, 0, 1);
        }
    }
}

INT_PTR CALLBACK NewDownloadDialog::DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG: {
        auto ctx = (NewDlgContext*)lParam;
        SetWindowLongPtrW(hDlg, DWLP_USER, (LONG_PTR)ctx);

        ForceForegroundWindow(hDlg);

        if (HttpEngine::IsSingleConnectionHost(ctx->initUrl)) {
            ctx->parts = 1;
        }

        // Setup spinner (1 to 12)
        SendDlgItemMessage(hDlg, IDC_SPN_PARTS, UDM_SETRANGE32, 1, 12);
        SendDlgItemMessage(hDlg, IDC_SPN_PARTS, UDM_SETPOS32, 0, ctx->parts);
        SetDlgItemInt(hDlg, IDC_EDT_PARTS, ctx->parts, FALSE);

        CheckDlgButton(hDlg, IDC_CHK_START_IMMEDIATE, ctx->startImmediate ? BST_CHECKED : BST_UNCHECKED);

        if (!ctx->initUrl.empty()) {
            SetDlgItemTextW(hDlg, IDC_EDT_URL, ctx->initUrl.c_str());
            std::wstring fname = ctx->initFilename.empty() ? HttpEngine::ExtractFilenameFromUrl(ctx->initUrl) : ctx->initFilename;
            bool isYouTube = (ctx->initUrl.find(L"youtube.com/") != std::wstring::npos || ctx->initUrl.find(L"youtu.be/") != std::wstring::npos);
            if (isYouTube && (fname.empty() || fname == L"watch")) {
                fname = L"youtube_video.mp4";
            }
            bool isHls = (ctx->initUrl.find(L".m3u8") != std::wstring::npos);
            if (isHls && fname.length() >= 5 && _wcsicmp(fname.c_str() + fname.length() - 5, L".m3u8") == 0) {
                fname = fname.substr(0, fname.length() - 5) + L".mp4";
            }
            std::wstring dir = Settings::Instance().GetTargetFolderForFilename(fname);
            WCHAR combined[MAX_PATH] = { 0 };
            PathCombineW(combined, dir.c_str(), fname.c_str());
            std::wstring uniquePath = DownloadManager::Instance().GenerateUniqueSavePath(combined);
            SetDlgItemTextW(hDlg, IDC_EDT_SAVE_PATH, uniquePath.c_str());
        } else {
            // Check clipboard for http:// or https:// URL
            if (OpenClipboard(hDlg)) {
                HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                if (hData) {
                    WCHAR* clipText = (WCHAR*)GlobalLock(hData);
                    if (clipText) {
                        std::wstring clipStr = clipText;
                        if (clipStr.rfind(L"http://", 0) == 0 || clipStr.rfind(L"https://", 0) == 0) {
                            SetDlgItemTextW(hDlg, IDC_EDT_URL, clipStr.c_str());
                            UpdateSavePathFromUrl(hDlg);
                        }
                        GlobalUnlock(hData);
                    }
                }
                CloseClipboard();
            }
        }

        // Focus URL edit
        SetFocus(GetDlgItem(hDlg, IDC_EDT_URL));
        SendDlgItemMessage(hDlg, IDC_EDT_URL, EM_SETSEL, 0, -1);
        return FALSE;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        WORD code = HIWORD(wParam);

        if (id == IDC_EDT_URL && code == EN_KILLFOCUS) {
            WCHAR pathBuf[MAX_PATH] = { 0 };
            GetDlgItemTextW(hDlg, IDC_EDT_SAVE_PATH, pathBuf, MAX_PATH);
            if (wcslen(pathBuf) == 0) {
                UpdateSavePathFromUrl(hDlg);
            }
        } else if (id == IDC_BTN_BROWSE) {
            WCHAR savePath[MAX_PATH] = { 0 };
            GetDlgItemTextW(hDlg, IDC_EDT_SAVE_PATH, savePath, MAX_PATH);

            OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
            ofn.hwndOwner = hDlg;
            ofn.lpstrFile = savePath;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"모든 파일 (*.*)\0*.*\0";
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

            if (GetSaveFileNameW(&ofn)) {
                SetDlgItemTextW(hDlg, IDC_EDT_SAVE_PATH, savePath);
            }
        } else if (id == IDOK) {
            auto ctx = (NewDlgContext*)GetWindowLongPtrW(hDlg, DWLP_USER);
            if (ctx) {
                WCHAR urlBuf[2048] = { 0 };
                WCHAR pathBuf[MAX_PATH] = { 0 };
                GetDlgItemTextW(hDlg, IDC_EDT_URL, urlBuf, 2048);
                GetDlgItemTextW(hDlg, IDC_EDT_SAVE_PATH, pathBuf, MAX_PATH);

                ctx->url = urlBuf;
                ctx->savePath = pathBuf;
                ctx->parts = GetDlgItemInt(hDlg, IDC_EDT_PARTS, NULL, FALSE);
                if (HttpEngine::IsSingleConnectionHost(ctx->url)) {
                    ctx->parts = 1;
                }
                if (ctx->parts < 1) ctx->parts = 1;
                if (ctx->parts > 12) ctx->parts = 12;
                ctx->startImmediate = (IsDlgButtonChecked(hDlg, IDC_CHK_START_IMMEDIATE) == BST_CHECKED);

                if (ctx->url.empty()) {
                    MessageBoxW(hDlg, L"다운로드 URL을 입력해 주세요.", L"알림", MB_OK | MB_ICONWARNING);
                    return TRUE;
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
