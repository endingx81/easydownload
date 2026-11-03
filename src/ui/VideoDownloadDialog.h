#pragma once
#include <windows.h>
#include <string>

namespace EasyDownload {

class VideoDownloadDialog {
public:
    static bool Show(
        HWND hParent,
        std::wstring& outStreamUrl,
        std::wstring& outSavePath,
        const std::wstring& initUrl = L"",
        const std::wstring& initTitle = L"",
        const std::wstring& referer = L"",
        const std::wstring& cookies = L""
    );

private:
    static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
};

} // namespace EasyDownload
