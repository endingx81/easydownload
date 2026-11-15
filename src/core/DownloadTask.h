#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <deque>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include <chrono>

namespace EasyDownload {

enum class TaskStatus {
    Queued,
    Connecting,
    Downloading,
    Paused,
    Completed,
    Error
};

struct ChunkProgress {
    int index{ 0 };
    int64_t startByte{ 0 };
    int64_t endByte{ 0 };
    int64_t downloadedBytes{ 0 };
    bool completed{ false };
};

struct RangeItem {
    int chunkIndex{ 0 };
    int64_t startByte{ 0 };
    int64_t endByte{ 0 };
};

class DownloadTask {
public:
    DownloadTask(
        const std::wstring& url,
        const std::wstring& savePath,
        const std::wstring& referer = L"",
        const std::wstring& cookies = L"",
        int numParts = 8
    );
    ~DownloadTask();

    void Start();
    void Pause();
    void Resume();
    void Cancel();
    void WaitForExit();

    std::wstring GetId() const { return m_id; }
    std::wstring GetUrl() const {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        return m_url;
    }
    void SetUrl(const std::wstring& url) {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        m_url = url;
    }
    std::wstring GetOriginalUrl() const {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        return !m_originalUrl.empty() ? m_originalUrl : m_url;
    }
    std::wstring GetSavePath() const {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        return m_savePath;
    }
    void SetSavePath(const std::wstring& path) {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        m_savePath = path;
    }
    std::wstring GetFilename() const {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        return m_filename;
    }
    void SetFilename(const std::wstring& name) {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        m_filename = name;
    }
    std::wstring GetReferer() const {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        return m_referer;
    }
    void SetReferer(const std::wstring& ref) {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        m_referer = ref;
    }
    std::wstring GetCookies() const {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        return m_cookies;
    }
    std::wstring GetDependsOnId() const {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        return m_dependsOnId;
    }
    void SetDependsOnId(const std::wstring& depId) {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        m_dependsOnId = depId;
    }

    int64_t GetTotalBytes() const { return m_totalBytes.load(); }
    void SetTotalBytes(int64_t total) { m_totalBytes.store(total); }
    int64_t GetDownloadedBytes() const { return m_downloadedBytes.load(); }
    int64_t GetCurrentSpeed() const { return m_currentSpeed.load(); }
    int64_t GetRemainingSeconds() const;
    int GetNumParts() const;
    int GetActiveWorkersCount() const { return m_activeWorkers.load(); }
    void SetActiveWorkersCount(int count) { m_activeWorkers.store(count); }
    TaskStatus GetStatus() const { return m_status.load(); }
    std::wstring GetStatusString() const;
    std::wstring GetErrorMessage() const {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        return m_errorMessage;
    }
    void SetErrorMessage(const std::wstring& msg) {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        m_errorMessage = msg;
    }
    std::wstring GetCreatedAt() const {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        return m_createdAt;
    }
    std::wstring GetCompletedAt() const {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        return m_completedAt;
    }
    bool IsHls() const { return m_isHls; }
    void SetIsHls(bool hls) { m_isHls = hls; }
    bool IsYouTube() const { return m_isYouTube; }
    void SetIsYouTube(bool yt) { m_isYouTube = yt; }

    std::vector<ChunkProgress> GetChunksProgress();

    void SetLoadedState(TaskStatus status);

    void SetOnStatusChanged(std::function<void(DownloadTask*)> callback) { m_onStatusChanged = callback; }

private:
    void WorkerLoop();
    void WorkerThreadFunc(HANDLE hTargetFile);
    void SaveResumeState();
    bool LoadResumeState();
    void CleanResumeState();

    std::wstring m_id;
    std::wstring m_url;
    std::wstring m_originalUrl;
    std::wstring m_referer;
    std::wstring m_cookies;
    std::wstring m_savePath;
    std::wstring m_filename;
    std::wstring m_dependsOnId;
    mutable std::mutex m_metaMutex;

    int m_numParts{ 8 };
    std::atomic<int64_t> m_totalBytes{ -1 };
    std::atomic<int64_t> m_downloadedBytes{ 0 };
    std::atomic<int64_t> m_currentSpeed{ 0 };
    std::atomic<bool> m_abortFlag{ false };
    std::atomic<bool> m_pauseFlag{ false };
    std::atomic<int> m_activeWorkers{ 0 };
    bool m_isHls{ false };
    bool m_isYouTube{ false };

    std::atomic<TaskStatus> m_status{ TaskStatus::Queued };
    std::wstring m_errorMessage;
    std::wstring m_createdAt;
    std::wstring m_completedAt;

    std::vector<ChunkProgress> m_chunks;
    std::mutex m_chunkMutex;

    std::atomic<int64_t> m_chunkDynamicEnds[32];
    std::atomic<int64_t> m_chunkBytesReceivedInWindow[32];
    std::atomic<bool> m_serverLimitHit{ false };

    std::deque<RangeItem> m_rangeQueue;
    std::mutex m_rangeMutex;

    std::mutex m_fileWriteMutex;

    std::thread m_mainThread;
    std::vector<std::thread> m_workerThreads;

    std::function<void(DownloadTask*)> m_onStatusChanged;

    std::chrono::steady_clock::time_point m_lastSpeedCalcTime;
    int64_t m_bytesSinceLastSpeedCalc{ 0 };
};

} // namespace EasyDownload
