#pragma once
#include <windows.h>
#include <memory>
#include <string>

namespace EasyDownload {

class DownloadTask;

class DownloadInfoDialog {
public:
    static HWND Show(HWND hParent, std::shared_ptr<DownloadTask> task);

private:
    static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
};

} // namespace EasyDownload

