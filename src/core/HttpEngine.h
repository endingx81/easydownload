#pragma once
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <functional>
#include <cstdint>
#include <atomic>
#include <mutex>

namespace EasyDownload {

struct HttpHeadInfo {
    int statusCode{ 0 };
    int64_t contentLength{ -1 };
    bool acceptRanges{ false };
    std::wstring contentType;
    std::wstring filename;
    std::wstring finalUrl;
};

class HttpEngine {
public:
    static bool QueryUrlInfo(
        const std::wstring& url,
        const std::wstring& referer,
        const std::wstring& cookies,
        HttpHeadInfo& outInfo
    );

    static bool DownloadRange(
        const std::wstring& url,
        const std::wstring& referer,
        const std::wstring& cookies,
        int64_t startByte,
        int64_t endByte,
        HANDLE hTargetFile,
        std::atomic<bool>& abortFlag,
        std::atomic<bool>& pauseFlag,
        std::function<void(int64_t bytesRead)> progressCallback,
        std::mutex* pFileMutex = nullptr
    );

    static bool DownloadBuffer(
        const std::wstring& url,
        const std::wstring& referer,
        const std::wstring& cookies,
        std::string& outData,
        std::atomic<bool>& abortFlag
    );

    static std::wstring ExtractFilenameFromUrl(const std::wstring& url);
    static std::wstring ExtractFilenameFromContentDisposition(const std::wstring& cdHeader);
    static std::wstring SanitizeFilename(const std::wstring& rawName, size_t maxLen = 0);
    static bool IsGenericFilename(const std::wstring& name);
    static std::wstring ExtractTitleFromRefererUrl(const std::wstring& refererUrl);
};

} // namespace EasyDownload
