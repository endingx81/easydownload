#pragma once
#include <windows.h>
#include <vector>
#include "../core/DownloadTask.h"

namespace EasyDownload {

class SegmentBar {
public:
    static bool Register();
    static HWND Create(HWND hParent, int x, int y, int width, int height, UINT id);

    static void SetChunks(HWND hWnd, const std::vector<ChunkProgress>& chunks, int64_t totalBytes);
    static void Clear(HWND hWnd);

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static void Paint(HWND hWnd, HDC hdc);
};

} // namespace EasyDownload
