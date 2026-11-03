#include "SegmentBar.h"
#include <string>

namespace EasyDownload {

static const WCHAR* SEGMENTBAR_CLASS = L"EasySegmentBarClass";

struct SegmentBarData {
    std::vector<ChunkProgress> chunks;
    int64_t totalBytes{ 0 };
};

bool SegmentBar::Register() {
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = SEGMENTBAR_CLASS;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.style = CS_HREDRAW | CS_VREDRAW;

    return RegisterClassExW(&wc) != 0;
}

HWND SegmentBar::Create(HWND hParent, int x, int y, int width, int height, UINT id) {
    Register();
    return CreateWindowExW(
        WS_EX_STATICEDGE,
        SEGMENTBAR_CLASS,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        x, y, width, height,
        hParent,
        (HMENU)(UINT_PTR)id,
        GetModuleHandle(NULL),
        NULL
    );
}

void SegmentBar::SetChunks(HWND hWnd, const std::vector<ChunkProgress>& chunks, int64_t totalBytes) {
    auto data = (SegmentBarData*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (!data) {
        data = new SegmentBarData();
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)data);
    }
    data->chunks = chunks;
    data->totalBytes = totalBytes;
    InvalidateRect(hWnd, NULL, FALSE);
}

void SegmentBar::Clear(HWND hWnd) {
    auto data = (SegmentBarData*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (data) {
        data->chunks.clear();
        data->totalBytes = 0;
        InvalidateRect(hWnd, NULL, FALSE);
    }
}

LRESULT CALLBACK SegmentBar::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        auto data = new SegmentBarData();
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)data);
        return 0;
    }
    case WM_DESTROY: {
        auto data = (SegmentBarData*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
        if (data) {
            delete data;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1; // Double-buffering handled in WM_PAINT
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        Paint(hWnd, hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

void SegmentBar::Paint(HWND hWnd, HDC hdc) {
    RECT rc;
    GetClientRect(hWnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return;

    // Double buffer
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    // Dark modern background
    HBRUSH bgBrush = CreateSolidBrush(RGB(30, 32, 40));
    FillRect(memDC, &rc, bgBrush);
    DeleteObject(bgBrush);

    // Border
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(55, 65, 81));
    HPEN oldPen = (HPEN)SelectObject(memDC, borderPen);
    SelectObject(memDC, GetStockObject(NULL_BRUSH));
    Rectangle(memDC, 0, 0, width, height);
    SelectObject(memDC, oldPen);
    DeleteObject(borderPen);

    auto data = (SegmentBarData*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (!data || data->chunks.empty()) {
        // Draw hint text
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, RGB(156, 163, 175));
        HFONT hFont = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT oldFont = (HFONT)SelectObject(memDC, hFont);

        RECT textRc = rc;
        const WCHAR* hint = L"다운로드 중인 작업의 실시간 멀티 청크 분할 현황이 여기에 표시됩니다.";
        DrawTextW(memDC, hint, -1, &textRc,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SelectObject(memDC, oldFont);
        DeleteObject(hFont);
    } else {
        int innerX = 2;
        int innerY = 2;
        int innerW = width - 4;
        int innerH = height - 4;

        HBRUSH emptyBrush = CreateSolidBrush(RGB(45, 50, 62));
        HBRUSH activeBrush = CreateSolidBrush(RGB(14, 165, 233));   // Vibrant Sky Blue
        HBRUSH doneBrush = CreateSolidBrush(RGB(16, 185, 129));     // Vibrant Emerald Green
        HPEN dividerPen = CreatePen(PS_SOLID, 1, RGB(30, 32, 40));

        HFONT hNumFont = CreateFontW(-10, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT oldFont = (HFONT)SelectObject(memDC, hNumFont);
        SetBkMode(memDC, TRANSPARENT);

        size_t n = data->chunks.size();

        // Calculate total chunk span across all chunks
        int64_t totalSpan = 0;
        for (size_t i = 0; i < n; ++i) {
            int64_t sz = (data->chunks[i].endByte - data->chunks[i].startByte) + 1;
            totalSpan += (sz > 0 ? sz : 1000);
        }
        if (totalSpan <= 0) totalSpan = 1;

        int currentX = innerX;
        for (size_t i = 0; i < n; ++i) {
            const auto& cp = data->chunks[i];
            int64_t chunkTotal = (cp.endByte - cp.startByte) + 1;
            if (chunkTotal <= 0) chunkTotal = 1000;

            int chunkW = (int)((double)chunkTotal / (double)totalSpan * innerW);
            if (i == n - 1) {
                chunkW = (innerX + innerW) - currentX; // exact fit to right border
            }
            if (chunkW < 2) chunkW = 2;

            RECT chunkRc = { currentX, innerY, currentX + chunkW, innerY + innerH };
            FillRect(memDC, &chunkRc, emptyBrush);

            double progRatio = (double)cp.downloadedBytes / (double)chunkTotal;
            if (progRatio > 1.0) progRatio = 1.0;

            if (cp.completed || progRatio >= 0.999) {
                FillRect(memDC, &chunkRc, doneBrush);
            } else if (cp.downloadedBytes > 0) {
                int fillW = (int)(progRatio * chunkW);
                if (fillW > 0) {
                    RECT fillRc = { currentX, innerY, currentX + fillW, innerY + innerH };
                    FillRect(memDC, &fillRc, activeBrush);
                }
            }

            // Draw divider between chunks
            if (i > 0) {
                HPEN oldP = (HPEN)SelectObject(memDC, dividerPen);
                MoveToEx(memDC, currentX, innerY, NULL);
                LineTo(memDC, currentX, innerY + innerH);
                SelectObject(memDC, oldP);
            }

            // Chunk label if wide enough
            if (chunkW >= 18) {
                SetTextColor(memDC, (cp.completed || cp.downloadedBytes > 0) ? RGB(255, 255, 255) : RGB(148, 163, 184));
                std::wstring numStr = std::to_wstring(i + 1);
                RECT numRc = chunkRc;
                DrawTextW(memDC, numStr.c_str(), -1, &numRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            currentX += chunkW;
        }

        SelectObject(memDC, oldFont);
        DeleteObject(hNumFont);
        DeleteObject(emptyBrush);
        DeleteObject(activeBrush);
        DeleteObject(doneBrush);
        DeleteObject(dividerPen);
    }

    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
}

} // namespace EasyDownload
