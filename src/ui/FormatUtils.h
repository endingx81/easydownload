#pragma once
#include <string>
#include <sstream>
#include <iomanip>
#include <cstdint>

namespace EasyDownload {

inline std::wstring FormatBytes(int64_t bytes) {
    if (bytes < 0) return L"알 수 없음";
    const double KB = 1024.0;
    const double MB = KB * 1024.0;
    const double GB = MB * 1024.0;

    std::wstringstream ss;
    ss << std::fixed << std::setprecision(1);

    if (bytes >= GB) {
        ss << (bytes / GB) << L" GB";
    } else if (bytes >= MB) {
        ss << (bytes / MB) << L" MB";
    } else if (bytes >= KB) {
        ss << (bytes / KB) << L" KB";
    } else {
        ss << bytes << L" B";
    }
    return ss.str();
}

inline std::wstring FormatSpeed(int64_t bytesPerSec) {
    if (bytesPerSec <= 0) return L"0 KB/s";
    const double KB = 1024.0;
    const double MB = KB * 1024.0;

    std::wstringstream ss;
    ss << std::fixed << std::setprecision(1);

    if (bytesPerSec >= MB) {
        ss << (bytesPerSec / MB) << L" MB/s";
    } else {
        ss << (bytesPerSec / KB) << L" KB/s";
    }
    return ss.str();
}

inline std::wstring FormatEta(int64_t seconds) {
    if (seconds < 0) return L"-";
    if (seconds == 0) return L"완료";

    int64_t h = seconds / 3600;
    int64_t m = (seconds % 3600) / 60;
    int64_t s = seconds % 60;

    std::wstringstream ss;
    if (h > 0) {
        ss << h << L"시간 " << m << L"분";
    } else if (m > 0) {
        ss << m << L"분 " << s << L"초";
    } else {
        ss << s << L"초";
    }
    return ss.str();
}

inline std::wstring FormatDuration(double seconds) {
    if (seconds <= 0.0) return L"-";
    int64_t totalSec = (int64_t)(seconds + 0.5);
    int64_t h = totalSec / 3600;
    int64_t m = (totalSec % 3600) / 60;
    int64_t s = totalSec % 60;

    std::wstringstream ss;
    if (h > 0) {
        ss << h << L":" << std::setfill(L'0') << std::setw(2) << m << L":" << std::setw(2) << s;
    } else {
        ss << std::setfill(L'0') << std::setw(2) << m << L":" << std::setw(2) << s;
    }
    return ss.str();
}

inline void ForceForegroundWindow(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) return;

    if (IsIconic(hWnd)) {
        ShowWindow(hWnd, SW_RESTORE);
    } else {
        ShowWindow(hWnd, SW_SHOW);
    }

    // Safe, non-blocking foreground elevation (NO AttachThreadInput deadlock!)
    AllowSetForegroundWindow(ASFW_ANY);
    SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetWindowPos(hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    BringWindowToTop(hWnd);
    SetForegroundWindow(hWnd);
    SetActiveWindow(hWnd);
    SetFocus(hWnd);
}

} // namespace EasyDownload

