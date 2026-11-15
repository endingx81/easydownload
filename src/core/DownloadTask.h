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

class SpeedTracker {
public:
    SpeedTracker() {
        Reset();
    }

    void Reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_samples.clear();
        m_smoothedSpeed = 0.0;
        m_lastReportedSpeed = 0;
        m_lastTotalBytes = -1;
        m_lastUpdateTime = std::chrono::steady_clock::time_point{};
    }

    int64_t Update(int64_t currentTotalBytes) {
        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_samples.empty() || m_lastTotalBytes < 0) {
            m_samples.push_back({ now, currentTotalBytes });
            m_lastUpdateTime = now;
            m_lastTotalBytes = currentTotalBytes;
            m_lastReportedSpeed = 0;
            m_smoothedSpeed = 0.0;
            return 0;
        }

        m_samples.push_back({ now, currentTotalBytes });

        // Maintain 2.0-second sliding time window
        auto cutoff = now - std::chrono::milliseconds(2000);
        while (m_samples.size() > 2 && m_samples.front().time < cutoff) {
            m_samples.pop_front();
        }

        int64_t rawSpeed = 0;
        if (m_samples.size() >= 2) {
            auto dtMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                m_samples.back().time - m_samples.front().time
            ).count();

            if (dtMs >= 150) {
                int64_t deltaBytes = m_samples.back().bytes - m_samples.front().bytes;
                if (deltaBytes < 0) deltaBytes = 0;
                rawSpeed = (deltaBytes * 1000) / dtMs;
            } else {
                rawSpeed = m_lastReportedSpeed;
            }
        }

        // Stall / pause detection
        if (currentTotalBytes > m_lastTotalBytes) {
            m_lastTotalBytes = currentTotalBytes;
            m_lastUpdateTime = now;
        } else {
            auto stallMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastUpdateTime).count();
            if (stallMs > 3000) {
                rawSpeed = 0;
                m_smoothedSpeed = 0.0;
                m_lastReportedSpeed = 0;
                return 0;
            } else if (stallMs > 1200) {
                rawSpeed = 0; // begin decaying
            }
        }

        // Exponential Moving Average (EMA) smoothing: alpha = 0.4
        if (m_smoothedSpeed <= 0.0) {
            m_smoothedSpeed = (double)rawSpeed;
        } else {
            m_smoothedSpeed = 0.4 * (double)rawSpeed + 0.6 * m_smoothedSpeed;
        }

        m_lastReportedSpeed = (int64_t)(m_smoothedSpeed + 0.5);
        return m_lastReportedSpeed;
    }

    int64_t GetSpeed() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lastReportedSpeed;
    }

    void SetDirectSpeed(int64_t speedBytes) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_smoothedSpeed <= 0.0) {
            m_smoothedSpeed = (double)speedBytes;
        } else {
            m_smoothedSpeed = 0.5 * (double)speedBytes + 0.5 * m_smoothedSpeed;
        }
        m_lastReportedSpeed = (int64_t)(m_smoothedSpeed + 0.5);
        m_lastUpdateTime = std::chrono::steady_clock::now();
    }

private:
    struct Sample {
        std::chrono::steady_clock::time_point time;
        int64_t bytes{ 0 };
    };

    mutable std::mutex m_mutex;
    std::deque<Sample> m_samples;
    double m_smoothedSpeed{ 0.0 };
    int64_t m_lastReportedSpeed{ 0 };
    int64_t m_lastTotalBytes{ -1 };
    std::chrono::steady_clock::time_point m_lastUpdateTime;
};

class DownloadTask {
public:
    DownloadTask(
        const std::wstring& url,
        const std::wstring& savePath,
        const std::wstring& referer = L"",
        const std::wstring& cookies = L"",
        int numParts = 8,
        const std::wstring& userAgent = L""
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
    void SetOriginalUrl(const std::wstring& origUrl) {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        m_originalUrl = origUrl;
    }
    std::wstring GetDisplayUrl() const;
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
    std::wstring GetUserAgent() const {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        return m_userAgent;
    }
    void SetUserAgent(const std::wstring& ua) {
        std::lock_guard<std::mutex> lock(m_metaMutex);
        m_userAgent = ua;
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
    std::wstring m_userAgent;
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

    SpeedTracker m_speedTracker;
};

} // namespace EasyDownload
