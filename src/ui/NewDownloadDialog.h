#pragma once
#include <windows.h>
#include <string>

namespace EasyDownload {

class NewDownloadDialog {
public:
    static bool Show(
        HWND hParent,
        std::wstring& outUrl,
        std::wstring& outSavePath,
        int& outParts,
        bool& outStartImmediate,
        const std::wstring& initUrl = L"",
        const std::wstring& initFilename = L""
    );

private:
    static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
};

} // namespace EasyDownload
