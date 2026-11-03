#pragma once
#include <windows.h>

namespace EasyDownload {

class SettingsDialog {
public:
    static void Show(HWND hParent);

private:
    static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
};

} // namespace EasyDownload
