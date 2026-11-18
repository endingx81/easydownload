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
    std::wstring initSegmentUrl; // Fragmented MP4 / CMAF initialization segment URL (#EXT-X-MAP)
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

    typedef std::function<void(int workerIdx, int workerCompleted, int workerTotal, int completedSegments, int totalSegments, int64_t bytesRead)> HlsProgressCallback;
    typedef std::function<bool(std::wstring& outNewToken)> TokenRefreshCallback;

    static bool DownloadHlsStream(
        const std::wstring& playlistUrl,
        const std::wstring& targetFilePath,
        const std::wstring& referer,
        const std::wstring& cookies,
        int maxWorkers,
        std::atomic<bool>& abortFlag,
        std::atomic<bool>& pauseFlag,
        HlsProgressCallback progressCallback,
        TokenRefreshCallback tokenRefresher = nullptr
    );

    static bool IsYtDlpAvailable(std::wstring& outPath);
    static std::wstring FindFfmpegLocation();
    static void UpdateYtDlpInBackground();
    static std::wstring CleanYouTubeUrl(const std::wstring& url);
    static std::wstring GetVideoTitleWithYtDlp(
        const std::wstring& videoPageUrl,
        const std::wstring& cookies = L""
    );

    typedef std::function<void(int percent, int64_t downloadedBytes, int64_t totalBytes, int64_t speedBytesPerSec)> YtDlpProgressCallback;

    static bool DownloadWithYtDlp(
        const std::wstring& videoPageUrl,
        const std::wstring& targetFilePath,
        std::atomic<bool>& abortFlag,
        YtDlpProgressCallback progressCallback,
        const std::wstring& cookies = L"",
        std::wstring* pOutError = nullptr,
        bool isAudioOnly = false,
        const std::wstring& audioFormat = L"mp3"
    );

private:
    static std::wstring ResolveUrl(const std::wstring& baseUrl, const std::wstring& relativeUrl);
};

} // namespace EasyDownload
