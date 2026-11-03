#include "DownloadTask.h"
#include "HttpEngine.h"
#include "HlsDownloader.h"
#include "Settings.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <algorithm>

namespace EasyDownload {

static std::wstring GenerateGuid() {
    GUID guid;
    CoCreateGuid(&guid);
    WCHAR guidBuf[64] = { 0 };
    StringFromGUID2(guid, guidBuf, 64);
    return guidBuf;
}

static std::wstring GetCurrentTimeString() {
    time_t now = time(nullptr);
    tm localTm;
    localtime_s(&localTm, &now);
    std::wstringstream ss;
    ss << std::put_time(&localTm, L"%Y-%m-%d %H:%M:%S");
    return ss.str();
}

DownloadTask::DownloadTask(
    const std::wstring& url,
    const std::wstring& savePath,
    const std::wstring& referer,
    const std::wstring& cookies,
    int numParts
) : m_url(url),
    m_savePath(savePath),
    m_referer(referer),
    m_cookies(cookies),
    m_numParts(numParts > 0 ? (numParts > 12 ? 12 : numParts) : Settings::Instance().GetInitialParts())
{
    m_id = GenerateGuid();
    m_createdAt = GetCurrentTimeString();
    m_adaptiveSpeedSplitting = Settings::Instance().GetAdaptiveSpeedSplitting();
    m_maxPartsLimit = Settings::Instance().GetMaxParts();

    WCHAR fname[MAX_PATH] = { 0 };
    wcscpy_s(fname, m_savePath.c_str());
    PathStripPathW(fname);
    m_filename = fname;

    if (m_filename.empty() || PathIsDirectoryW(m_savePath.c_str())) {
        m_filename = HttpEngine::ExtractFilenameFromUrl(m_url);
        if (PathIsDirectoryW(m_savePath.c_str())) {
            WCHAR combined[MAX_PATH] = { 0 };
            PathCombineW(combined, m_savePath.c_str(), m_filename.c_str());
            m_savePath = combined;
        }
    }

    m_isHls = (m_url.find(L".m3u8") != std::wstring::npos);
    if (m_isHls) {
        std::lock_guard<std::mutex> lock(m_chunkMutex);
        m_chunks.clear();
        for (int i = 0; i < 8; ++i) {
            ChunkProgress cp;
            cp.index = i;
            cp.startByte = i * 1000;
            cp.endByte = (i + 1) * 1000 - 1;
            cp.downloadedBytes = 0;
            cp.completed = false;
            m_chunks.push_back(cp);
        }
    }
}

DownloadTask::~DownloadTask() {
    Cancel();
    if (m_mainThread.joinable()) {
        m_mainThread.join();
    }
}

void DownloadTask::Start() {
    if (m_status == TaskStatus::Downloading) return;

    m_abortFlag = false;
    m_pauseFlag = false;
    m_status = TaskStatus::Connecting;
    if (m_onStatusChanged) m_onStatusChanged(this);

    if (m_mainThread.joinable()) {
        m_mainThread.join();
    }
    m_mainThread = std::thread(&DownloadTask::WorkerLoop, this);
}

void DownloadTask::Pause() {
    if (m_status != TaskStatus::Downloading && m_status != TaskStatus::Connecting) return;
    m_pauseFlag = true;
    m_status = TaskStatus::Paused;
    if (m_onStatusChanged) m_onStatusChanged(this);
}

void DownloadTask::Resume() {
    if (m_status == TaskStatus::Paused || m_status == TaskStatus::Error) {
        Start();
    }
}

void DownloadTask::Cancel() {
    m_abortFlag = true;
    m_pauseFlag = false;
    for (auto& t : m_workerThreads) {
        if (t.joinable()) t.join();
    }
    m_workerThreads.clear();
}

int DownloadTask::GetNumParts() const {
    if (!m_chunks.empty()) return (int)m_chunks.size();
    return m_numParts;
}

std::wstring DownloadTask::GetStatusString() const {
    switch (m_status) {
    case TaskStatus::Queued: return L"대기 중";
    case TaskStatus::Connecting: return L"연결 중...";
    case TaskStatus::Downloading: return L"다운로드 중";
    case TaskStatus::Paused: return L"일시 정지";
    case TaskStatus::Completed: return L"완료됨";
    case TaskStatus::Error: return L"오류";
    default: return L"알 수 없음";
    }
}

int64_t DownloadTask::GetRemainingSeconds() const {
    int64_t speed = m_currentSpeed.load();
    if (speed <= 0 || m_totalBytes <= 0) return -1;
    int64_t remainingBytes = m_totalBytes - m_downloadedBytes.load();
    if (remainingBytes <= 0) return 0;
    return remainingBytes / speed;
}

std::vector<ChunkProgress> DownloadTask::GetChunksProgress() {
    std::lock_guard<std::mutex> lock(m_chunkMutex);
    return m_chunks;
}

void DownloadTask::SaveResumeState() {
    std::wstring partFile = m_savePath + L".egpart";
    std::ofstream out(partFile, std::ios::binary | std::ios::trunc);
    if (!out) return;

    out << m_totalBytes << "\n";
    std::lock_guard<std::mutex> lock(m_chunkMutex);
    out << m_chunks.size() << "\n";
    for (const auto& cp : m_chunks) {
        out << cp.index << " " << cp.startByte << " " << cp.endByte << " "
            << cp.downloadedBytes << " " << (cp.completed ? 1 : 0) << "\n";
    }
}

bool DownloadTask::LoadResumeState() {
    std::wstring partFile = m_savePath + L".egpart";
    std::ifstream in(partFile, std::ios::binary);
    if (!in) return false;

    int64_t totalBytes = 0;
    size_t chunkCount = 0;
    if (!(in >> totalBytes >> chunkCount)) return false;
    if (totalBytes != m_totalBytes || chunkCount == 0) return false;

    std::vector<ChunkProgress> loaded;
    int64_t totalDown = 0;
    for (size_t i = 0; i < chunkCount; ++i) {
        ChunkProgress cp;
        int compVal = 0;
        if (!(in >> cp.index >> cp.startByte >> cp.endByte >> cp.downloadedBytes >> compVal)) {
            return false;
        }
        cp.completed = (compVal != 0);
        totalDown += cp.downloadedBytes;
        loaded.push_back(cp);
    }

    std::lock_guard<std::mutex> lock(m_chunkMutex);
    m_chunks = loaded;
    m_downloadedBytes = totalDown;
    return true;
}

void DownloadTask::CleanResumeState() {
    std::wstring partFile = m_savePath + L".egpart";
    DeleteFileW(partFile.c_str());
}

bool DownloadTask::TryStealWork(RangeItem& outRange) {
    std::lock_guard<std::mutex> lock(m_chunkMutex);
    if ((int)m_chunks.size() >= m_maxPartsLimit) return false;

    int bestIdx = -1;
    int64_t maxRemaining = 0;

    for (size_t i = 0; i < m_chunks.size(); ++i) {
        if (m_chunks[i].completed) continue;
        int64_t curStart = m_chunks[i].startByte + m_chunks[i].downloadedBytes;
        int64_t rem = m_chunks[i].endByte - curStart + 1;
        if (rem > maxRemaining) {
            maxRemaining = rem;
            bestIdx = (int)i;
        }
    }

    // Split if remaining is at least 2 MB
    if (bestIdx >= 0 && maxRemaining >= (2 * 1024 * 1024)) {
        int64_t curStart = m_chunks[bestIdx].startByte + m_chunks[bestIdx].downloadedBytes;
        int64_t half = maxRemaining / 2;
        int64_t newStart = curStart + half;
        int64_t oldEnd = m_chunks[bestIdx].endByte;

        m_chunks[bestIdx].endByte = newStart - 1;

        ChunkProgress newChunk;
        newChunk.index = (int)m_chunks.size();
        newChunk.startByte = newStart;
        newChunk.endByte = oldEnd;
        newChunk.downloadedBytes = 0;
        newChunk.completed = false;
        m_chunks.push_back(newChunk);

        outRange.chunkIndex = newChunk.index;
        outRange.startByte = newStart;
        outRange.endByte = oldEnd;
        return true;
    }

    return false;
}

void DownloadTask::WorkerThreadFunc(HANDLE hTargetFile, std::atomic<int>& activeWorkers) {
    while (!m_abortFlag && !m_pauseFlag) {
        RangeItem taskRange = { -1, 0, -1 };
        bool hasRange = false;

        {
            std::lock_guard<std::mutex> lock(m_rangeMutex);
            if (!m_rangeQueue.empty()) {
                taskRange = m_rangeQueue.front();
                m_rangeQueue.pop_front();
                hasRange = true;
            }
        }

        if (!hasRange) {
            // Try to steal work from chunks that have plenty of bytes left!
            if (TryStealWork(taskRange)) {
                hasRange = true;
            } else {
                // No more work in queue or to steal
                break;
            }
        }

        // Download this range with aggressive retries
        int64_t currentOffset = taskRange.startByte;
        int retryCount = 0;
        const int maxRetries = 5;

        while (currentOffset <= taskRange.endByte && !m_abortFlag && !m_pauseFlag) {
            // Check dynamic chunk endByte in case it was split by work-stealing
            int64_t effectiveEndByte = taskRange.endByte;
            {
                std::lock_guard<std::mutex> lock(m_chunkMutex);
                if (taskRange.chunkIndex >= 0 && taskRange.chunkIndex < (int)m_chunks.size()) {
                    effectiveEndByte = m_chunks[taskRange.chunkIndex].endByte;
                }
            }
            if (currentOffset > effectiveEndByte) {
                std::lock_guard<std::mutex> lock(m_chunkMutex);
                if (taskRange.chunkIndex >= 0 && taskRange.chunkIndex < (int)m_chunks.size()) {
                    m_chunks[taskRange.chunkIndex].completed = true;
                }
                break;
            }

            bool ok = HttpEngine::DownloadRange(
                m_url,
                m_referer,
                m_cookies,
                currentOffset,
                effectiveEndByte,
                hTargetFile,
                m_abortFlag,
                m_pauseFlag,
                [this, taskRange, &currentOffset](int64_t bytesRead) {
                    currentOffset += bytesRead;
                    m_downloadedBytes += bytesRead;
                    m_bytesSinceLastSpeedCalc += bytesRead;
                    {
                        std::lock_guard<std::mutex> lock(m_chunkMutex);
                        if (taskRange.chunkIndex >= 0 && taskRange.chunkIndex < (int)m_chunks.size()) {
                            m_chunks[taskRange.chunkIndex].downloadedBytes += bytesRead;
                            if (m_chunks[taskRange.chunkIndex].startByte + m_chunks[taskRange.chunkIndex].downloadedBytes > m_chunks[taskRange.chunkIndex].endByte) {
                                m_chunks[taskRange.chunkIndex].completed = true;
                            }
                        }
                    }
                }
            );

            if (ok || currentOffset > effectiveEndByte) {
                // Range finished
                std::lock_guard<std::mutex> lock(m_chunkMutex);
                if (taskRange.chunkIndex >= 0 && taskRange.chunkIndex < (int)m_chunks.size()) {
                    m_chunks[taskRange.chunkIndex].completed = true;
                }
                break;
            }

            if (m_abortFlag || m_pauseFlag) break;

            // Connection broke or failed, retry with backoff
            retryCount++;
            if (retryCount >= maxRetries) {
                // Push unfinished range back into queue so another surviving worker can take it over!
                if (currentOffset <= taskRange.endByte) {
                    std::lock_guard<std::mutex> lock(m_rangeMutex);
                    RangeItem unfinished = { taskRange.chunkIndex, currentOffset, taskRange.endByte };
                    m_rangeQueue.push_back(unfinished);
                }
                break;
            }

            // Exponential backoff sleep (1s, 2s, 3s...)
            std::this_thread::sleep_for(std::chrono::milliseconds(800 * retryCount));
        }
    }

    activeWorkers--;
}

void DownloadTask::WorkerLoop() {
    bool isHls = m_isHls || (m_url.find(L".m3u8") != std::wstring::npos);
    if (isHls) {
        WCHAR dir[MAX_PATH] = { 0 };
        wcscpy_s(dir, m_savePath.c_str());
        PathRemoveFileSpecW(dir);
        SHCreateDirectoryExW(NULL, dir, NULL);

        m_status = TaskStatus::Downloading;
        if (m_onStatusChanged) m_onStatusChanged(this);

        m_lastSpeedCalcTime = std::chrono::steady_clock::now();
        m_bytesSinceLastSpeedCalc = 0;
        int64_t lastReportedBytes = 0;

        // Initialize visual segment chunks
        {
            std::lock_guard<std::mutex> lock(m_chunkMutex);
            m_chunks.clear();
            for (int i = 0; i < 8; ++i) {
                ChunkProgress cp;
                cp.index = i;
                cp.startByte = i * 1000;
                cp.endByte = (i + 1) * 1000 - 1;
                cp.downloadedBytes = 0;
                cp.completed = false;
                m_chunks.push_back(cp);
            }
        }

        bool ok = HlsDownloader::DownloadHlsStream(
            m_url,
            m_savePath,
            m_referer,
            m_cookies,
            8,
            m_abortFlag,
            m_pauseFlag,
            [this, &lastReportedBytes](int completedSegs, int totalSegs, int64_t bytesRead) {
                int64_t delta = bytesRead - lastReportedBytes;
                lastReportedBytes = bytesRead;
                if (delta > 0) {
                    m_bytesSinceLastSpeedCalc += delta;
                }
                m_downloadedBytes = bytesRead;

                auto now = std::chrono::steady_clock::now();
                auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastSpeedCalcTime).count();
                if (elapsedMs >= 500) {
                    m_currentSpeed = (m_bytesSinceLastSpeedCalc * 1000) / elapsedMs;
                    m_bytesSinceLastSpeedCalc = 0;
                    m_lastSpeedCalcTime = now;
                }

                if (totalSegs > 0) {
                    int64_t avg = bytesRead / (completedSegs > 0 ? completedSegs : 1);
                    m_totalBytes = avg * totalSegs;
                }

                // Update visual segment chunks
                {
                    std::lock_guard<std::mutex> lock(m_chunkMutex);
                    double ratio = (double)completedSegs / (double)(totalSegs > 0 ? totalSegs : 1);
                    int activeChunk = (int)(ratio * 8.0);
                    if (activeChunk > 8) activeChunk = 8;
                    for (int i = 0; i < 8; ++i) {
                        int64_t chunkTotal = (m_chunks[i].endByte - m_chunks[i].startByte) + 1;
                        if (i < activeChunk) {
                            m_chunks[i].completed = true;
                            m_chunks[i].downloadedBytes = chunkTotal;
                        } else if (i == activeChunk) {
                            m_chunks[i].completed = false;
                            double subRatio = (ratio * 8.0) - activeChunk;
                            m_chunks[i].downloadedBytes = (int64_t)(subRatio * chunkTotal);
                        } else {
                            m_chunks[i].completed = false;
                            m_chunks[i].downloadedBytes = 0;
                        }
                    }
                }
            }
        );

        m_currentSpeed = 0;

        if (ok) {
            m_status = TaskStatus::Completed;
            m_completedAt = GetCurrentTimeString();
            m_totalBytes = m_downloadedBytes.load();
            {
                std::lock_guard<std::mutex> lock(m_chunkMutex);
                for (auto& cp : m_chunks) {
                    cp.completed = true;
                    cp.downloadedBytes = (cp.endByte - cp.startByte) + 1;
                }
            }
        } else {
            if (!m_abortFlag && !m_pauseFlag) {
                m_status = TaskStatus::Error;
                m_errorMessage = L"HLS 스트림 다운로드 실패 (세그먼트 수신 차단 또는 재생 목록 오류)";
            }
        }

        if (m_onStatusChanged) m_onStatusChanged(this);
        return;
    }

    HttpHeadInfo info;
    if (!HttpEngine::QueryUrlInfo(m_url, m_referer, m_cookies, info)) {
        m_status = TaskStatus::Error;
        m_errorMessage = L"서버에 연결할 수 없거나 잘못된 URL입니다.";
        if (m_onStatusChanged) m_onStatusChanged(this);
        return;
    }

    if (info.statusCode >= 400) {
        m_status = TaskStatus::Error;
        m_errorMessage = L"HTTP 오류 코드: " + std::to_wstring(info.statusCode);
        if (m_onStatusChanged) m_onStatusChanged(this);
        return;
    }

    m_totalBytes = info.contentLength;

    WCHAR dir[MAX_PATH] = { 0 };
    wcscpy_s(dir, m_savePath.c_str());
    PathRemoveFileSpecW(dir);
    SHCreateDirectoryExW(NULL, dir, NULL);

    bool useRange = info.acceptRanges && (m_totalBytes > 1024 * 512) && (m_numParts > 1);

    HANDLE hTargetFile = CreateFileW(
        m_savePath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hTargetFile == INVALID_HANDLE_VALUE) {
        m_status = TaskStatus::Error;
        m_errorMessage = L"대상 파일을 생성할 수 없습니다. 쓰기 권한을 확인하세요.";
        if (m_onStatusChanged) m_onStatusChanged(this);
        return;
    }

    if (m_totalBytes > 0) {
        LARGE_INTEGER li;
        li.QuadPart = m_totalBytes;
        SetFilePointerEx(hTargetFile, li, NULL, FILE_BEGIN);
        SetEndOfFile(hTargetFile);
    }

    {
        std::lock_guard<std::mutex> rLock(m_rangeMutex);
        m_rangeQueue.clear();
    }

    if (useRange) {
        bool hasResume = LoadResumeState();
        if (!hasResume) {
            std::lock_guard<std::mutex> lock(m_chunkMutex);
            m_chunks.clear();
            int actualParts = m_numParts > 12 ? 12 : (m_numParts < 1 ? 1 : m_numParts);
            int64_t chunkSize = m_totalBytes / actualParts;

            for (int i = 0; i < actualParts; ++i) {
                ChunkProgress cp;
                cp.index = i;
                cp.startByte = i * chunkSize;
                cp.endByte = (i == actualParts - 1) ? (m_totalBytes - 1) : ((i + 1) * chunkSize - 1);
                cp.downloadedBytes = 0;
                cp.completed = false;
                m_chunks.push_back(cp);

                RangeItem item = { i, cp.startByte, cp.endByte };
                m_rangeQueue.push_back(item);
            }
            m_downloadedBytes = 0;
        } else {
            std::lock_guard<std::mutex> lock(m_chunkMutex);
            for (auto& cp : m_chunks) {
                if (!cp.completed) {
                    int64_t start = cp.startByte + cp.downloadedBytes;
                    if (start <= cp.endByte) {
                        RangeItem item = { cp.index, start, cp.endByte };
                        m_rangeQueue.push_back(item);
                    } else {
                        cp.completed = true;
                    }
                }
            }
        }
    } else {
        std::lock_guard<std::mutex> lock(m_chunkMutex);
        m_chunks.clear();
        ChunkProgress cp;
        cp.index = 0;
        cp.startByte = 0;
        cp.endByte = m_totalBytes > 0 ? (m_totalBytes - 1) : -1;
        cp.downloadedBytes = 0;
        cp.completed = false;
        m_chunks.push_back(cp);

        RangeItem item = { 0, 0, cp.endByte };
        m_rangeQueue.push_back(item);
    }

    m_status = TaskStatus::Downloading;
    if (m_onStatusChanged) m_onStatusChanged(this);

    m_lastSpeedCalcTime = std::chrono::steady_clock::now();
    m_lastRampCheckTime = m_lastSpeedCalcTime;
    m_bytesSinceLastSpeedCalc = 0;
    m_lastRampCheckSpeed = 0;
    m_rampUpFinished = !m_adaptiveSpeedSplitting;

    m_workerThreads.clear();
    std::atomic<int> activeWorkers{ 0 };

    int threadCount = (int)m_chunks.size();
    if (threadCount > m_maxPartsLimit) threadCount = m_maxPartsLimit;
    if (threadCount < 1) threadCount = 1;

    for (int i = 0; i < threadCount; ++i) {
        activeWorkers++;
        m_workerThreads.emplace_back(&DownloadTask::WorkerThreadFunc, this, hTargetFile, std::ref(activeWorkers));
    }

    // Dynamic supervisor loop
    while (!m_abortFlag && !m_pauseFlag) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        auto now = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastSpeedCalcTime).count();
        if (elapsedMs >= 500) {
            int64_t speed = (m_bytesSinceLastSpeedCalc * 1000) / elapsedMs;
            m_currentSpeed = speed;
            m_bytesSinceLastSpeedCalc = 0;
            m_lastSpeedCalcTime = now;
        }

        // Adaptive speed-ramping check (every ~2.5s)
        auto rampElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastRampCheckTime).count();
        if (m_adaptiveSpeedSplitting && !m_rampUpFinished && rampElapsedMs >= 2500) {
            m_lastRampCheckTime = now;
            int64_t curSpeed = m_currentSpeed.load();

            int64_t remBytes = (m_totalBytes > 0) ? (m_totalBytes - m_downloadedBytes.load()) : 0;
            int curWorkers = activeWorkers.load();

            if (curWorkers < m_maxPartsLimit && remBytes >= (3 * 1024 * 1024) && curSpeed > 0) {
                if (m_lastRampCheckSpeed == 0) {
                    m_lastRampCheckSpeed = curSpeed;
                    RangeItem stealRange;
                    if (TryStealWork(stealRange)) {
                        {
                            std::lock_guard<std::mutex> rLock(m_rangeMutex);
                            m_rangeQueue.push_back(stealRange);
                        }
                        activeWorkers++;
                        m_workerThreads.emplace_back(&DownloadTask::WorkerThreadFunc, this, hTargetFile, std::ref(activeWorkers));
                    }
                } else {
                    if (curSpeed > (int64_t)(m_lastRampCheckSpeed * 1.12)) {
                        m_lastRampCheckSpeed = curSpeed;
                        RangeItem stealRange;
                        if (TryStealWork(stealRange)) {
                            {
                                std::lock_guard<std::mutex> rLock(m_rangeMutex);
                                m_rangeQueue.push_back(stealRange);
                            }
                            activeWorkers++;
                            m_workerThreads.emplace_back(&DownloadTask::WorkerThreadFunc, this, hTargetFile, std::ref(activeWorkers));
                        } else {
                            m_rampUpFinished = true;
                        }
                    } else {
                        m_rampUpFinished = true;
                    }
                }
            } else if (curWorkers >= m_maxPartsLimit || remBytes < (3 * 1024 * 1024)) {
                m_rampUpFinished = true;
            }
        }

        SaveResumeState();

        // If all workers finished, break
        if (activeWorkers.load() == 0) {
            // Check if queue has remaining items
            bool hasRemaining = false;
            {
                std::lock_guard<std::mutex> rLock(m_rangeMutex);
                hasRemaining = !m_rangeQueue.empty();
            }

            if (hasRemaining && !m_abortFlag && !m_pauseFlag) {
                // If all workers died due to connection limits but queue still has ranges,
                // revive a single worker to sequentially complete the missing parts!
                activeWorkers++;
                m_workerThreads.emplace_back(&DownloadTask::WorkerThreadFunc, this, hTargetFile, std::ref(activeWorkers));
                continue;
            }

            break;
        }
    }

    for (auto& t : m_workerThreads) {
        if (t.joinable()) t.join();
    }
    m_workerThreads.clear();

    CloseHandle(hTargetFile);
    m_currentSpeed = 0;

    if (m_abortFlag) return;

    if (m_pauseFlag) {
        SaveResumeState();
        m_status = TaskStatus::Paused;
        if (m_onStatusChanged) m_onStatusChanged(this);
        return;
    }

    // Verify all chunks
    bool allDone = true;
    {
        std::lock_guard<std::mutex> lock(m_chunkMutex);
        for (const auto& cp : m_chunks) {
            if (!cp.completed && (cp.endByte >= cp.startByte)) {
                allDone = false;
                break;
            }
        }
    }

    if (allDone || (m_totalBytes > 0 && m_downloadedBytes.load() >= m_totalBytes)) {
        m_status = TaskStatus::Completed;
        m_completedAt = GetCurrentTimeString();
        CleanResumeState();
        {
            std::lock_guard<std::mutex> lock(m_chunkMutex);
            for (auto& cp : m_chunks) {
                cp.completed = true;
                if (cp.endByte >= cp.startByte) {
                    cp.downloadedBytes = (cp.endByte - cp.startByte) + 1;
                }
            }
        }
    } else {
        m_status = TaskStatus::Error;
        m_errorMessage = L"일부 세그먼트 다운로드가 중단되었습니다. 재개하여 계속할 수 있습니다.";
        SaveResumeState();
    }

    if (m_onStatusChanged) m_onStatusChanged(this);
}

} // namespace EasyDownload
