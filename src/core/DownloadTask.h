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

    std::wstring GetId() const { return m_id; }
    std::wstring GetUrl() const { return m_url; }
    std::wstring GetSavePath() const { return m_savePath; }
    std::wstring GetFilename() const { return m_filename; }
    int64_t GetTotalBytes() const { return m_totalBytes; }
    int64_t GetDownloadedBytes() const { return m_downloadedBytes.load(); }
    int64_t GetCurrentSpeed() const { return m_currentSpeed.load(); }
    int64_t GetRemainingSeconds() const;
    int GetNumParts() const;
    int GetActiveWorkersCount() const { return m_activeWorkers.load(); }
    void SetActiveWorkersCount(int count) { m_activeWorkers.store(count); }
    TaskStatus GetStatus() const { return m_status; }
    std::wstring GetStatusString() const;
    std::wstring GetErrorMessage() const { return m_errorMessage; }
    std::wstring GetCreatedAt() const { return m_createdAt; }
    std::wstring GetCompletedAt() const { return m_completedAt; }
    bool IsHls() const { return m_isHls; }
    void SetIsHls(bool hls) { m_isHls = hls; }
    bool IsYouTube() const { return m_isYouTube; }
    void SetIsYouTube(bool yt) { m_isYouTube = yt; }

    std::vector<ChunkProgress> GetChunksProgress();

    void SetOnStatusChanged(std::function<void(DownloadTask*)> callback) { m_onStatusChanged = callback; }

private:
    void WorkerLoop();
    void WorkerThreadFunc(HANDLE hTargetFile);
    void SaveResumeState();
    bool LoadResumeState();
    void CleanResumeState();

    std::wstring m_id;
    std::wstring m_url;
    std::wstring m_referer;
    std::wstring m_cookies;
    std::wstring m_savePath;
    std::wstring m_filename;
    int m_numParts{ 8 };
    int64_t m_totalBytes{ -1 };
    std::atomic<int64_t> m_downloadedBytes{ 0 };
    std::atomic<int64_t> m_currentSpeed{ 0 };
    std::atomic<bool> m_abortFlag{ false };
    std::atomic<bool> m_pauseFlag{ false };
    std::atomic<int> m_activeWorkers{ 0 };
    bool m_isHls{ false };
    bool m_isYouTube{ false };

    TaskStatus m_status{ TaskStatus::Queued };
    std::wstring m_errorMessage;
    std::wstring m_createdAt;
    std::wstring m_completedAt;

    std::vector<ChunkProgress> m_chunks;
    std::mutex m_chunkMutex;

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
