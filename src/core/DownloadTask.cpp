#include "DownloadTask.h"
#include "HttpEngine.h"
#include "HlsDownloader.h"
#include "SextbExtractor.h"
#include "Av01Extractor.h"
#include "LinkExtractor.h"
#include "Settings.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <winioctl.h>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <algorithm>

namespace EasyDownload {

static bool IsBinaryArchiveFile(const std::wstring& filename) {
    if (filename.length() < 4) return false;
    std::wstring lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    const wchar_t* exts[] = {
        L".zip", L".rar", L".7z", L".tar", L".gz", L".iso", L".bin", L".exe",
        L".nup", L".nsp", L".xci", L".pkg", L".mp4", L".mkv", L".avi", L".mov",
        L".001", L".002", L".003", L".004", L".005", L".006", L".007", L".008", L".009", L".010",
        L".011", L".012", L".013", L".014", L".015"
    };
    for (const auto* ext : exts) {
        if (lower.length() >= wcslen(ext) &&
            lower.compare(lower.length() - wcslen(ext), wcslen(ext), ext) == 0) {
            return true;
        }
    }
    return false;
}

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
    m_originalUrl(url),
    m_savePath(savePath),
    m_referer(referer),
    m_cookies(cookies),
    m_numParts(numParts > 0 ? (numParts > 12 ? 12 : numParts) : 8)
{
    m_id = GenerateGuid();
    m_createdAt = GetCurrentTimeString();

    for (int i = 0; i < 32; ++i) {
        m_chunkDynamicEnds[i].store(-1);
        m_chunkBytesReceivedInWindow[i].store(0);
    }

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

    m_isHls = (m_url.find(L".m3u8") != std::wstring::npos) ||
              (m_url.find(L"playlist.txt") != std::wstring::npos) ||
              (m_url.find(L"index.txt") != std::wstring::npos) ||
              (m_url.find(L"master.txt") != std::wstring::npos);
    if (m_isHls) {
        // Enforce .mp4 extension for HLS downloads (never save as .m3u8 or .txt playlist file!)
        if (m_filename.length() >= 5 && _wcsicmp(m_filename.c_str() + m_filename.length() - 5, L".m3u8") == 0) {
            m_filename = m_filename.substr(0, m_filename.length() - 5) + L".mp4";
        } else if (m_filename.length() >= 4 && _wcsicmp(m_filename.c_str() + m_filename.length() - 4, L".txt") == 0) {
            m_filename = m_filename.substr(0, m_filename.length() - 4) + L".mp4";
        }
        if (m_savePath.length() >= 5 && _wcsicmp(m_savePath.c_str() + m_savePath.length() - 5, L".m3u8") == 0) {
            m_savePath = m_savePath.substr(0, m_savePath.length() - 5) + L".mp4";
        } else if (m_savePath.length() >= 4 && _wcsicmp(m_savePath.c_str() + m_savePath.length() - 4, L".txt") == 0) {
            m_savePath = m_savePath.substr(0, m_savePath.length() - 4) + L".mp4";
        }

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
    m_speedTracker.Reset();
    m_currentSpeed = 0;
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
    m_speedTracker.Reset();
    m_currentSpeed = 0;
    SaveResumeState();
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
    m_status = TaskStatus::Paused;
    m_speedTracker.Reset();
    m_currentSpeed = 0;
    SaveResumeState();
    if (m_onStatusChanged) m_onStatusChanged(this);
}

void DownloadTask::WaitForExit() {
    if (m_mainThread.joinable() && std::this_thread::get_id() != m_mainThread.get_id()) {
        m_mainThread.join();
    }
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
    case TaskStatus::Paused: return L"중단됨";
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

void DownloadTask::SetLoadedState(TaskStatus status) {
    m_status = status;

    // Check if this is an HLS download with existing segments in cacheDir
    std::wstring cacheDir = m_savePath + L".hls_parts";
    if (PathFileExistsW(cacheDir.c_str())) {
        m_isHls = true;
        WIN32_FIND_DATAW wfd;
        std::wstring searchPattern = cacheDir + L"\\*.*";
        HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &wfd);
        if (hFind != INVALID_HANDLE_VALUE) {
            int64_t totalBytesInCache = 0;
            do {
                if (!(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    totalBytesInCache += ((int64_t)wfd.nFileSizeHigh << 32) | wfd.nFileSizeLow;
                }
            } while (FindNextFileW(hFind, &wfd));
            FindClose(hFind);
            if (totalBytesInCache > 0) {
                m_downloadedBytes = totalBytesInCache;
            }
        }
    } else {
        // Check regular egpart resume state
        LoadResumeState();
    }
}

void DownloadTask::WorkerThreadFunc(HANDLE hTargetFile) {
    m_activeWorkers++;

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
            break;
        }

        int64_t currentOffset = taskRange.startByte;
        int retryCount = 0;
        const int maxRetries = 5;

        while ((taskRange.endByte < 0 || currentOffset <= taskRange.endByte) && !m_abortFlag && !m_pauseFlag) {
            const std::atomic<int64_t>* pDynEnd = (taskRange.chunkIndex >= 0 && taskRange.chunkIndex < 32)
                ? &m_chunkDynamicEnds[taskRange.chunkIndex]
                : nullptr;

            bool ok = HttpEngine::DownloadRange(
                m_url,
                m_referer,
                m_cookies,
                currentOffset,
                taskRange.endByte,
                hTargetFile,
                m_abortFlag,
                m_pauseFlag,
                [this, taskRange, &currentOffset](int64_t bytesRead) {
                    currentOffset += bytesRead;
                    m_downloadedBytes += bytesRead;
                    if (taskRange.chunkIndex >= 0 && taskRange.chunkIndex < 32) {
                        m_chunkBytesReceivedInWindow[taskRange.chunkIndex] += bytesRead;
                    }
                    {
                        std::lock_guard<std::mutex> lock(m_chunkMutex);
                        if (taskRange.chunkIndex >= 0 && taskRange.chunkIndex < (int)m_chunks.size()) {
                            m_chunks[taskRange.chunkIndex].downloadedBytes += bytesRead;
                            int64_t chunkEnd = m_chunks[taskRange.chunkIndex].endByte;
                            if (chunkEnd >= 0 && m_chunks[taskRange.chunkIndex].startByte + m_chunks[taskRange.chunkIndex].downloadedBytes > chunkEnd) {
                                m_chunks[taskRange.chunkIndex].completed = true;
                            }
                        }
                    }
                },
                &m_fileWriteMutex,
                pDynEnd,
                &m_serverLimitHit
            );

            if (ok || (taskRange.endByte >= 0 && currentOffset > taskRange.endByte)) {
                // Range or single stream finished
                std::lock_guard<std::mutex> lock(m_chunkMutex);
                if (taskRange.chunkIndex >= 0 && taskRange.chunkIndex < (int)m_chunks.size()) {
                    m_chunks[taskRange.chunkIndex].completed = true;
                    if (taskRange.endByte < 0) {
                        m_chunks[taskRange.chunkIndex].endByte = currentOffset > 0 ? (currentOffset - 1) : 0;
                    }
                }
                break;
            }

            if (m_abortFlag || m_pauseFlag) break;

            // Connection broke or failed, retry with backoff
            retryCount++;
            if (retryCount >= maxRetries) {
                // Push unfinished range back into queue so another surviving worker can take it over
                if (taskRange.endByte < 0 || currentOffset <= taskRange.endByte) {
                    std::lock_guard<std::mutex> lock(m_rangeMutex);
                    RangeItem unfinished = { taskRange.chunkIndex, currentOffset, taskRange.endByte };
                    m_rangeQueue.push_back(unfinished);
                }
                break;
            }

            // Backoff sleep (500ms, 1000ms...)
            std::this_thread::sleep_for(std::chrono::milliseconds(500 * retryCount));
        }
    }

    m_activeWorkers--;
}

void DownloadTask::WorkerLoop() {
    bool isYouTube = m_isYouTube ||
        (m_url.find(L"youtube.com/watch") != std::wstring::npos) ||
        (m_url.find(L"youtu.be/") != std::wstring::npos) ||
        (m_url.find(L"youtube.com/shorts/") != std::wstring::npos);

    if (isYouTube) {
        m_isYouTube = true;
        m_status = TaskStatus::Downloading;
        if (m_onStatusChanged) m_onStatusChanged(this);

        // 1. Fetch real video title with yt-dlp if available
        std::wstring realTitle = HlsDownloader::GetVideoTitleWithYtDlp(m_url);
        if (!realTitle.empty()) {
            std::lock_guard<std::mutex> lk(m_metaMutex);
            m_filename = realTitle + L".mp4";
            WCHAR dir[MAX_PATH] = { 0 };
            wcscpy_s(dir, m_savePath.c_str());
            PathRemoveFileSpecW(dir);
            m_savePath = std::wstring(dir) + L"\\" + m_filename;
        }

        WCHAR dir[MAX_PATH] = { 0 };
        wcscpy_s(dir, m_savePath.c_str());
        PathRemoveFileSpecW(dir);
        SHCreateDirectoryExW(NULL, dir, NULL);

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

        m_activeWorkers = 1;
        m_totalBytes = -1;
        m_downloadedBytes = 0;
        m_currentSpeed = 0;

        bool ok = HlsDownloader::DownloadWithYtDlp(
            m_url,
            m_savePath,
            m_abortFlag,
            [this](int percent, int64_t downBytes, int64_t totalBytes, int64_t speedBytes) {
                if (percent < 0) percent = 0;
                if (percent > 100) percent = 100;

                if (totalBytes > 0) {
                    m_totalBytes = totalBytes;
                    m_downloadedBytes = downBytes;
                } else if (downBytes > 0) {
                    m_downloadedBytes = downBytes;
                }

                if (speedBytes > 0) {
                    m_speedTracker.SetDirectSpeed(speedBytes);
                    m_currentSpeed = m_speedTracker.GetSpeed();
                }

                int activeBlocks = (percent * 8) / 100;
                {
                    std::lock_guard<std::mutex> lock(m_chunkMutex);
                    for (int i = 0; i < (int)m_chunks.size(); ++i) {
                        if (i < activeBlocks) {
                            m_chunks[i].downloadedBytes = 1000;
                            m_chunks[i].completed = true;
                        } else if (i == activeBlocks) {
                            int rem = (percent * 8) % 100;
                            m_chunks[i].downloadedBytes = rem * 10;
                            m_chunks[i].completed = false;
                        } else {
                            m_chunks[i].downloadedBytes = 0;
                            m_chunks[i].completed = false;
                        }
                    }
                }
                if (m_onStatusChanged) m_onStatusChanged(this);
            }
        );

        m_activeWorkers = 0;

        if (ok) {
            m_status = TaskStatus::Completed;
            m_speedTracker.Reset();
            m_currentSpeed = 0;
            auto now = std::chrono::system_clock::now();
            auto in_time_t = std::chrono::system_clock::to_time_t(now);
            struct tm timeinfo;
            localtime_s(&timeinfo, &in_time_t);
            WCHAR timeBuf[64] = { 0 };
            wcsftime(timeBuf, 64, L"%Y-%m-%d %H:%M:%S", &timeinfo);
            {
                std::lock_guard<std::mutex> lk(m_metaMutex);
                m_completedAt = timeBuf;
            }

            WIN32_FILE_ATTRIBUTE_DATA fad = { 0 };
            if (GetFileAttributesExW(m_savePath.c_str(), GetFileExInfoStandard, &fad)) {
                LARGE_INTEGER li;
                li.LowPart = fad.nFileSizeLow;
                li.HighPart = fad.nFileSizeHigh;
                m_totalBytes = li.QuadPart;
                m_downloadedBytes = li.QuadPart;
            } else {
                m_totalBytes = 100;
                m_downloadedBytes = 100;
            }

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
                std::lock_guard<std::mutex> lk(m_metaMutex);
                m_errorMessage = L"YouTube 다운로드 실패 (yt-dlp 실행 오류 또는 차단)";
            } else {
                m_status = TaskStatus::Paused;
                m_speedTracker.Reset();
                m_currentSpeed = 0;
            }
        }

        if (m_onStatusChanged) m_onStatusChanged(this);
        return;
    }

    bool isSextb = SextbExtractor::IsSupported(m_url);
    if (isSextb) {
        m_status = TaskStatus::Downloading;
        if (m_onStatusChanged) m_onStatusChanged(this);

        std::wstring realStreamUrl;
        std::wstring realTitle;
        std::wstring realReferer;
        if (SextbExtractor::ExtractStream(m_url, realStreamUrl, realTitle, realReferer, m_abortFlag)) {
            {
                std::lock_guard<std::mutex> lk(m_metaMutex);
                m_url = realStreamUrl;
                m_referer = realReferer;
                if (!realTitle.empty()) {
                    m_filename = realTitle + L".mp4";
                    WCHAR dir[MAX_PATH] = { 0 };
                    wcscpy_s(dir, m_savePath.c_str());
                    PathRemoveFileSpecW(dir);
                    m_savePath = std::wstring(dir) + L"\\" + m_filename;
                }
            }
            m_isHls = true;
            if (m_onStatusChanged) m_onStatusChanged(this);
        } else {
            m_status = TaskStatus::Error;
            {
                std::lock_guard<std::mutex> lk(m_metaMutex);
                m_errorMessage = L"sextb.net 동영상 스트림 추출 실패";
            }
            if (m_onStatusChanged) m_onStatusChanged(this);
            return;
        }
    }

    std::wstring testUrl = !m_originalUrl.empty() ? m_originalUrl : m_url;
    bool isAv01 = Av01Extractor::IsSupported(testUrl);
    if (isAv01) {
        m_status = TaskStatus::Downloading;
        if (m_onStatusChanged) m_onStatusChanged(this);

        std::wstring realStreamUrl;
        std::wstring realTitle;
        std::wstring realReferer;
        if (Av01Extractor::ExtractStream(testUrl, realStreamUrl, realTitle, realReferer, m_abortFlag)) {
            {
                std::lock_guard<std::mutex> lk(m_metaMutex);
                m_url = realStreamUrl;
                m_referer = realReferer;
                if (!realTitle.empty() && m_filename.empty()) {
                    m_filename = realTitle + L".mp4";
                    WCHAR dir[MAX_PATH] = { 0 };
                    wcscpy_s(dir, m_savePath.c_str());
                    PathRemoveFileSpecW(dir);
                    m_savePath = std::wstring(dir) + L"\\" + m_filename;
                }
            }
            m_isHls = true;
            if (m_onStatusChanged) m_onStatusChanged(this);
        } else {
            m_status = TaskStatus::Error;
            {
                std::lock_guard<std::mutex> lk(m_metaMutex);
                m_errorMessage = L"av01.media 동영상 스트림 추출 실패";
            }
            if (m_onStatusChanged) m_onStatusChanged(this);
            return;
        }
    }

    bool isHls = m_isHls ||
        (m_url.find(L".m3u8") != std::wstring::npos) ||
        (m_url.find(L"playlist.txt") != std::wstring::npos) ||
        (m_url.find(L"index.txt") != std::wstring::npos) ||
        (m_url.find(L"master.txt") != std::wstring::npos);
    if (isHls) {
        WCHAR dir[MAX_PATH] = { 0 };
        wcscpy_s(dir, m_savePath.c_str());
        PathRemoveFileSpecW(dir);
        SHCreateDirectoryExW(NULL, dir, NULL);

        m_status = TaskStatus::Downloading;
        if (m_onStatusChanged) m_onStatusChanged(this);

        m_speedTracker.Reset();
        m_speedTracker.Update(0);

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

        m_activeWorkers = 8;
        HlsDownloader::TokenRefreshCallback tokenRefresher = nullptr;
        if (Av01Extractor::IsSupported(testUrl)) {
            std::wstring videoId = Av01Extractor::ExtractVideoId(testUrl);
            if (!videoId.empty()) {
                tokenRefresher = [this, videoId](std::wstring& outNewToken) -> bool {
                    return Av01Extractor::FetchFreshAccessToken(videoId, outNewToken, m_abortFlag);
                };
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
            [this](int workerIdx, int workerCompleted, int workerTotal, int completedSegs, int totalSegs, int64_t bytesRead) {
                m_downloadedBytes = bytesRead;
                m_currentSpeed = m_speedTracker.Update(bytesRead);

                if (totalSegs > 0) {
                    int64_t avg = bytesRead / (completedSegs > 0 ? completedSegs : 1);
                    m_totalBytes = avg * totalSegs;
                }

                // Update visual segment chunks in parallel!
                {
                    std::lock_guard<std::mutex> lock(m_chunkMutex);
                    if (workerIdx >= 0 && workerIdx < (int)m_chunks.size()) {
                        int64_t chunkTotal = (m_chunks[workerIdx].endByte - m_chunks[workerIdx].startByte) + 1;
                        if (workerTotal > 0) {
                            double prog = (double)workerCompleted / (double)workerTotal;
                            if (prog > 1.0) prog = 1.0;
                            m_chunks[workerIdx].downloadedBytes = (int64_t)(prog * chunkTotal);
                            if (workerCompleted >= workerTotal) {
                                m_chunks[workerIdx].completed = true;
                                m_chunks[workerIdx].downloadedBytes = chunkTotal;
                            }
                        }
                    }
                }
            },
            tokenRefresher
        );
        m_activeWorkers = 0;
        m_speedTracker.Reset();
        m_currentSpeed = 0;

        if (ok) {
            m_status = TaskStatus::Completed;
            m_currentSpeed = 0;
            {
                std::lock_guard<std::mutex> lk(m_metaMutex);
                m_completedAt = GetCurrentTimeString();
                if (m_savePath.length() >= 5 && _wcsicmp(m_savePath.c_str() + m_savePath.length() - 5, L".m3u8") == 0) {
                    m_savePath = m_savePath.substr(0, m_savePath.length() - 5) + L".mp4";
                    m_filename = HttpEngine::ExtractFilenameFromUrl(m_savePath);
                }
            }
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
                std::lock_guard<std::mutex> lk(m_metaMutex);
                m_errorMessage = L"HLS 스트림 다운로드 실패 (세그먼트 수신 차단 또는 재생 목록 오류)";
            } else {
                m_status = TaskStatus::Paused;
                m_currentSpeed = 0;
            }
        }

        if (m_onStatusChanged) m_onStatusChanged(this);
        return;
    }

    // JIT (Just-In-Time) Dynamic Link Resolution before connecting
    std::wstring orig = GetOriginalUrl();
    if (!orig.empty()) {
        bool shouldResolve = false;
        if (orig.find(L"nswvault.com") != std::wstring::npos ||
            orig.find(L"send.now") != std::wstring::npos ||
            orig.find(L"send.cm") != std::wstring::npos ||
            orig.find(L"1fichier.com") != std::wstring::npos ||
            orig.find(L"megaup.net") != std::wstring::npos) {
            // If current URL is intermediate landing page, resolve fresh direct link right now
            if (m_url == orig ||
                m_url.find(L"nswvault.com") != std::wstring::npos ||
                m_url.find(L"1fichier.com/?") != std::wstring::npos ||
                m_url.find(L"send.now/") != std::wstring::npos ||
                m_url.find(L"megaup.net/") != std::wstring::npos) {
                shouldResolve = true;
            }
        }

        if (shouldResolve) {
            ExtractedLink fresh;
            fresh.originalUrl = orig;
            fresh.filename = m_filename;
            if (LinkExtractor::ResolveLink(fresh, m_abortFlag)) {
                if (!fresh.directUrl.empty()) {
                    std::lock_guard<std::mutex> lk(m_metaMutex);
                    m_url = fresh.directUrl;
                    if (!fresh.filename.empty() && HttpEngine::IsGenericFilename(m_filename)) {
                        m_filename = fresh.filename;
                    }
                }
            }
        }
    }

    HttpHeadInfo info;
    bool queryOk = HttpEngine::QueryUrlInfo(m_url, m_referer, m_cookies, info);

    // If query failed or returned 401/403/404/410, try dynamic re-resolution once in case direct URL expired (TTL)
    if (!queryOk || info.statusCode >= 400) {
        if (!orig.empty() && orig != m_url) {
            ExtractedLink retryLink;
            retryLink.originalUrl = orig;
            retryLink.filename = m_filename;
            if (LinkExtractor::ResolveLink(retryLink, m_abortFlag) && !retryLink.directUrl.empty()) {
                {
                    std::lock_guard<std::mutex> lk(m_metaMutex);
                    m_url = retryLink.directUrl;
                }
                queryOk = HttpEngine::QueryUrlInfo(m_url, m_referer, m_cookies, info);
            }
        }
    }

    if (!queryOk) {
        m_status = TaskStatus::Error;
        {
            std::lock_guard<std::mutex> lk(m_metaMutex);
            m_errorMessage = L"서버에 연결할 수 없거나 잘못된 URL입니다.";
        }
        if (m_onStatusChanged) m_onStatusChanged(this);
        return;
    }

    if (info.statusCode >= 400) {
        m_status = TaskStatus::Error;
        {
            std::lock_guard<std::mutex> lk(m_metaMutex);
            if (info.statusCode == 403 && (m_url.find(L"megaup.net") != std::wstring::npos || orig.find(L"megaup.net") != std::wstring::npos)) {
                m_errorMessage = L"인간 확인(Cloudflare Turnstile) 차단 페이지 감지 (더블클릭하여 브라우저에서 인증 필요)";
            } else {
                m_errorMessage = L"HTTP 오류 코드: " + std::to_wstring(info.statusCode);
            }
        }
        if (m_onStatusChanged) m_onStatusChanged(this);
        return;
    }

    // HTML Landing Page Guard:
    // If the expected target file is a binary archive/media (.zip, .rar, .001, .002, .nup, etc.),
    // but the server returned 'text/html' with small size (< 10MB), do NOT save a fake HTML file!
    std::wstring contentTypeLower = info.contentType;
    std::transform(contentTypeLower.begin(), contentTypeLower.end(), contentTypeLower.begin(), ::tolower);
    bool isHtml = (contentTypeLower.find(L"text/html") != std::wstring::npos);
    bool isBinary = IsBinaryArchiveFile(m_filename) || IsBinaryArchiveFile(m_savePath) || IsBinaryArchiveFile(orig);

    if (isHtml && isBinary && info.contentLength < 10 * 1024 * 1024) {
        m_status = TaskStatus::Error;
        {
            std::lock_guard<std::mutex> lk(m_metaMutex);
            m_errorMessage = L"웹페이지 또는 인간 확인(Cloudflare) 차단 페이지 감지 (더블클릭하여 브라우저에서 인증 필요)";
        }
        if (m_onStatusChanged) m_onStatusChanged(this);
        return;
    }

    {
        std::lock_guard<std::mutex> lk(m_metaMutex);
        if (!info.finalUrl.empty()) {
            m_url = info.finalUrl;
        }

        if (!info.filename.empty() && (HttpEngine::IsGenericFilename(m_filename) || m_filename == L"download.bin")) {
            m_filename = info.filename;
            WCHAR dir[MAX_PATH] = { 0 };
            wcscpy_s(dir, m_savePath.c_str());
            PathRemoveFileSpecW(dir);
            m_savePath = std::wstring(dir) + L"\\" + m_filename;
        }
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
        // Mark as sparse file so Windows doesn't synchronously zero-fill gigabytes of disk clusters (instant 0ms allocation!)
        DWORD dwBytesReturned = 0;
        DeviceIoControl(
            hTargetFile,
            FSCTL_SET_SPARSE,
            NULL,
            0,
            NULL,
            0,
            &dwBytesReturned,
            NULL
        );

        LARGE_INTEGER li;
        li.QuadPart = m_totalBytes;
        SetFilePointerEx(hTargetFile, li, NULL, FILE_BEGIN);
        SetEndOfFile(hTargetFile);
    }

    {
        std::lock_guard<std::mutex> rLock(m_rangeMutex);
        m_rangeQueue.clear();
    }

    std::wstring checkUrl = GetUrl();
    std::wstring checkOrigUrl = GetOriginalUrl();
    bool isSingleHost = HttpEngine::IsSingleConnectionHost(checkUrl, checkOrigUrl);

    if (useRange) {
        auto& settings = Settings::Instance();
        bool adaptive = isSingleHost ? false : settings.GetAdaptiveSpeedSplitting();
        int initialParts = settings.GetInitialParts();
        int maxParts = settings.GetMaxParts();

        bool hasResume = LoadResumeState();
        if (!hasResume) {
            std::lock_guard<std::mutex> lock(m_chunkMutex);
            m_chunks.clear();
            int actualParts = isSingleHost ? 1 : (adaptive ? initialParts : m_numParts);
            if (actualParts > maxParts) actualParts = maxParts;
            if (actualParts > 12) actualParts = 12;
            if (actualParts < 1) actualParts = 1;
            int64_t chunkSize = m_totalBytes / actualParts;

            for (int i = 0; i < actualParts; ++i) {
                ChunkProgress cp;
                cp.index = i;
                cp.startByte = i * chunkSize;
                cp.endByte = (i == actualParts - 1) ? (m_totalBytes - 1) : ((i + 1) * chunkSize - 1);
                cp.downloadedBytes = 0;
                cp.completed = false;
                m_chunks.push_back(cp);

                if (i < 32) {
                    m_chunkDynamicEnds[i].store(cp.endByte);
                    m_chunkBytesReceivedInWindow[i].store(0);
                }

                RangeItem item = { i, cp.startByte, cp.endByte };
                m_rangeQueue.push_back(item);
            }
            m_downloadedBytes = 0;
        } else {
            std::lock_guard<std::mutex> lock(m_chunkMutex);
            for (auto& cp : m_chunks) {
                if (cp.index < 32) {
                    m_chunkDynamicEnds[cp.index].store(cp.endByte);
                    m_chunkBytesReceivedInWindow[cp.index].store(0);
                }
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

        RangeItem item = { 0, 0, (info.acceptRanges ? cp.endByte : -1) };
        m_rangeQueue.push_back(item);
    }

    m_status = TaskStatus::Downloading;
    if (m_onStatusChanged) m_onStatusChanged(this);

    m_speedTracker.Reset();
    m_speedTracker.Update(m_downloadedBytes.load());

    m_workerThreads.clear();

    int threadCount = (int)m_chunks.size();
    if (threadCount > 12) threadCount = 12;
    if (threadCount < 1) threadCount = 1;

    for (int i = 0; i < threadCount; ++i) {
        m_workerThreads.emplace_back(&DownloadTask::WorkerThreadFunc, this, hTargetFile);
    }

    auto& settings = Settings::Instance();
    bool adaptive = isSingleHost ? false : settings.GetAdaptiveSpeedSplitting();
    int maxParts = settings.GetMaxParts();
    int speedTier = settings.GetInternetSpeedTier();

    int64_t saturationThreshold = INT64_MAX;
    if (speedTier == 1) saturationThreshold = 11000000LL;        // 100 Mbps (~11 MB/s)
    else if (speedTier == 2) saturationThreshold = 55000000LL;   // 500 Mbps (~55 MB/s)
    else if (speedTier == 3) saturationThreshold = 110000000LL;  // 1 Gbps (~110 MB/s)
    else if (speedTier == 4) saturationThreshold = 260000000LL;  // 2.5 Gbps+ (~260 MB/s)

    int adaptiveCheckTicks = 0;
    int64_t prevSpeed = 0;
    int stagnantSeconds = 0;
    int cooldownSeconds = 3; // Initial 3s warm-up for initial connections to ramp up

    int lastAddedChunkIdx = -1;
    bool awaitingEval = false;
    int evalCountdown = 0;
    int64_t speedBeforeAdd = 0;
    bool splitHalted = false; // Set to true if server limits connections or new connection is dead

    // Monitoring loop
    while (!m_abortFlag && !m_pauseFlag) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        m_currentSpeed = m_speedTracker.Update(m_downloadedBytes.load());

        // Smart Adaptive Acceleration: Ramp up connections to maximize final speed,
        // but verify that each newly added connection is actually receiving data before adding more!
        if (adaptive && useRange && !m_abortFlag && !m_pauseFlag && !splitHalted) {
            adaptiveCheckTicks++;
            if (adaptiveCheckTicks >= 4) { // every ~1000ms
                adaptiveCheckTicks = 0;
                int64_t currentSpeed = m_currentSpeed.load();

                // 1. Did server reject connections with 429/403/503?
                if (m_serverLimitHit.load()) {
                    splitHalted = true;
                }
                // 2. Bandwidth saturation threshold reached?
                else if (currentSpeed >= saturationThreshold) {
                    stagnantSeconds = 0;
                    prevSpeed = currentSpeed;
                }
                // 3. Evaluation of previously added connection
                else if (awaitingEval) {
                    evalCountdown--;
                    if (evalCountdown <= 0) {
                        awaitingEval = false;
                        cooldownSeconds = 2; // short stabilization cooldown

                        int64_t newChunkBytes = 0;
                        if (lastAddedChunkIdx >= 0 && lastAddedChunkIdx < 32) {
                            newChunkBytes = m_chunkBytesReceivedInWindow[lastAddedChunkIdx].load();
                        }

                        // Check if server rejected or new worker received 0 bytes
                        if (newChunkBytes == 0 || m_serverLimitHit.load()) {
                            // The server refused or failed this extra connection!
                            // Immediately halt further connection attempts!
                            splitHalted = true;
                        }
                        // Check if total speed dropped drastically (> 20% drop from before adding)
                        else if (speedBeforeAdd > 0 && currentSpeed < (int64_t)(speedBeforeAdd * 0.8)) {
                            splitHalted = true;
                        }
                        else {
                            // New connection is successfully downloading bytes (even at 10KB/s)!
                            // Allow further scaling!
                            prevSpeed = currentSpeed;
                            stagnantSeconds = 0;
                        }
                    }
                }
                // 4. Cooldown
                else if (cooldownSeconds > 0) {
                    cooldownSeconds--;
                    prevSpeed = currentSpeed;
                }
                // 5. Check if accelerating or stagnant
                else {
                    bool isAccelerating = false;
                    if (prevSpeed > 0) {
                        int64_t gain = currentSpeed - prevSpeed;
                        if (gain > (int64_t)(prevSpeed * 0.05)) {
                            isAccelerating = true;
                        }
                    }

                    if (isAccelerating) {
                        stagnantSeconds = 0;
                        prevSpeed = currentSpeed;
                    } else {
                        stagnantSeconds++;
                        prevSpeed = currentSpeed;

                        if (stagnantSeconds >= 2 && (int)m_chunks.size() < maxParts && !splitHalted) {
                            bool workersHealthy = (m_activeWorkers.load() > 0);

                            std::lock_guard<std::mutex> lock(m_chunkMutex);
                            int largestIdx = -1;
                            int64_t maxRemaining = 0;

                            for (size_t i = 0; i < m_chunks.size(); ++i) {
                                if (!m_chunks[i].completed) {
                                    int64_t curHead = m_chunks[i].startByte + m_chunks[i].downloadedBytes;
                                    int64_t rem = m_chunks[i].endByte - curHead;
                                    if (rem > maxRemaining && rem >= (6 * 1024 * 1024)) {
                                        maxRemaining = rem;
                                        largestIdx = (int)i;
                                    }
                                }
                            }

                            if (workersHealthy && largestIdx >= 0 && (int)m_chunks.size() < maxParts) {
                                int64_t curHead = m_chunks[largestIdx].startByte + m_chunks[largestIdx].downloadedBytes;
                                int64_t oldEnd = m_chunks[largestIdx].endByte;
                                int64_t half = (oldEnd - curHead) / 2;
                                int64_t splitPoint = curHead + half;

                                m_chunks[largestIdx].endByte = splitPoint - 1;
                                if (largestIdx < 32) {
                                    m_chunkDynamicEnds[largestIdx].store(splitPoint - 1);
                                }

                                int newIdx = (int)m_chunks.size();
                                ChunkProgress newCp;
                                newCp.index = newIdx;
                                newCp.startByte = splitPoint;
                                newCp.endByte = oldEnd;
                                newCp.downloadedBytes = 0;
                                newCp.completed = false;
                                m_chunks.push_back(newCp);

                                if (newIdx < 32) {
                                    m_chunkDynamicEnds[newIdx].store(oldEnd);
                                    m_chunkBytesReceivedInWindow[newIdx].store(0);
                                }

                                {
                                    std::lock_guard<std::mutex> rLock(m_rangeMutex);
                                    RangeItem item = { newCp.index, newCp.startByte, newCp.endByte };
                                    m_rangeQueue.push_back(item);
                                }

                                m_workerThreads.emplace_back(&DownloadTask::WorkerThreadFunc, this, hTargetFile);

                                lastAddedChunkIdx = newIdx;
                                speedBeforeAdd = currentSpeed;
                                awaitingEval = true;
                                evalCountdown = 4; // 4 seconds probation window
                                stagnantSeconds = 0;
                                prevSpeed = currentSpeed;

                                if (m_onStatusChanged) m_onStatusChanged(this);
                            }
                        }
                    }
                }
            }
        }

        SaveResumeState();

        if (m_activeWorkers.load() == 0) {
            break;
        }
    }

    for (auto& t : m_workerThreads) {
        if (t.joinable()) t.join();
    }
    m_workerThreads.clear();

    CloseHandle(hTargetFile);
    m_speedTracker.Reset();
    m_currentSpeed = 0;

    if (m_abortFlag || m_pauseFlag) {
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
            if (!cp.completed) {
                allDone = false;
                break;
            }
        }
    }

    if (allDone || (m_totalBytes > 0 && m_downloadedBytes.load() >= m_totalBytes)) {
        m_status = TaskStatus::Completed;
        {
            std::lock_guard<std::mutex> lk(m_metaMutex);
            m_completedAt = GetCurrentTimeString();
        }
        CleanResumeState();
        if (m_totalBytes <= 0) {
            m_totalBytes = m_downloadedBytes.load();
        }
        {
            std::lock_guard<std::mutex> lock(m_chunkMutex);
            for (auto& cp : m_chunks) {
                cp.completed = true;
                if (cp.endByte >= cp.startByte) {
                    cp.downloadedBytes = (cp.endByte - cp.startByte) + 1;
                } else {
                    cp.downloadedBytes = m_downloadedBytes.load();
                    cp.endByte = cp.downloadedBytes > 0 ? (cp.downloadedBytes - 1) : 0;
                }
            }
        }
    } else {
        m_status = TaskStatus::Error;
        {
            std::lock_guard<std::mutex> lk(m_metaMutex);
            m_errorMessage = L"일부 세그먼트 다운로드가 중단되었습니다. 재개하여 계속할 수 있습니다.";
        }
        SaveResumeState();
    }

    if (m_onStatusChanged) m_onStatusChanged(this);
}

} // namespace EasyDownload
