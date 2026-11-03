#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <atomic>

namespace EasyDownload {

struct StreamQuality {
    std::wstring url;
    std::wstring resolution;
    int bandwidth{ 0 };
    int64_t estimatedBytes{ 0 };
    std::wstring codecs;
};

struct HlsPlaylistInfo {
    bool isMaster{ false };
    std::vector<StreamQuality> qualities;
    std::vector<std::wstring> segmentUrls;
    double totalDuration{ 0.0 };
};

class HlsDownloader {
public:
    static bool ParsePlaylist(
        const std::wstring& playlistUrl,
        const std::wstring& referer,
        const std::wstring& cookies,
        HlsPlaylistInfo& outInfo,
        std::atomic<bool>& abortFlag
    );

    static bool DownloadHlsStream(
        const std::wstring& playlistUrl,
        const std::wstring& targetFilePath,
        const std::wstring& referer,
        const std::wstring& cookies,
        int maxWorkers,
        std::atomic<bool>& abortFlag,
        std::atomic<bool>& pauseFlag,
        std::function<void(int completedSegments, int totalSegments, int64_t bytesRead)> progressCallback
    );

    static bool IsYtDlpAvailable(std::wstring& outPath);
    static bool DownloadWithYtDlp(
        const std::wstring& videoPageUrl,
        const std::wstring& targetFilePath,
        std::atomic<bool>& abortFlag,
        std::function<void(int percent, const std::wstring& speedStr)> progressCallback
    );

private:
    static std::wstring ResolveUrl(const std::wstring& baseUrl, const std::wstring& relativeUrl);
};

} // namespace EasyDownload
