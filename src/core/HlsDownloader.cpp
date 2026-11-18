#include "HlsDownloader.h"
#include "HttpEngine.h"
#include <sstream>
#include <iostream>
#include <fstream>
#include <thread>
#include <mutex>
#include <shlwapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <ctime>

namespace EasyDownload {

std::wstring HlsDownloader::ResolveUrl(const std::wstring& baseUrl, const std::wstring& relativeUrl) {
    if (relativeUrl.rfind(L"http://", 0) == 0 || relativeUrl.rfind(L"https://", 0) == 0) {
        return relativeUrl;
    }
    if (relativeUrl.rfind(L"//", 0) == 0) {
        size_t colon = baseUrl.find(L":");
        if (colon != std::wstring::npos) {
            return baseUrl.substr(0, colon + 1) + relativeUrl;
        }
        return L"https:" + relativeUrl;
    }

    URL_COMPONENTS urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = (DWORD)-1;
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;

    if (!WinHttpCrackUrl(baseUrl.c_str(), (DWORD)baseUrl.length(), 0, &urlComp)) {
        return relativeUrl;
    }

    std::wstring scheme(urlComp.lpszScheme, urlComp.dwSchemeLength);
    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring root = scheme + L"://" + host;
    if ((urlComp.nScheme == INTERNET_SCHEME_HTTP && urlComp.nPort != 80) ||
        (urlComp.nScheme == INTERNET_SCHEME_HTTPS && urlComp.nPort != 443)) {
        root += L":" + std::to_wstring(urlComp.nPort);
    }

    if (relativeUrl.rfind(L"/", 0) == 0) {
        return root + relativeUrl;
    }

    std::wstring basePath = (urlComp.lpszUrlPath && urlComp.dwUrlPathLength > 0)
        ? std::wstring(urlComp.lpszUrlPath, urlComp.dwUrlPathLength)
        : L"/";
    size_t lastSlash = basePath.find_last_of(L'/');
    if (lastSlash != std::wstring::npos) {
        basePath = basePath.substr(0, lastSlash + 1);
    } else {
        basePath = L"/";
    }

    return root + basePath + relativeUrl;
}

static std::wstring TrimLine(const std::wstring& line) {
    size_t first = line.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return L"";
    size_t last = line.find_last_not_of(L" \t\r\n");
    return line.substr(first, (last - first + 1));
}

bool HlsDownloader::ParsePlaylist(
    const std::wstring& playlistUrl,
    const std::wstring& referer,
    const std::wstring& cookies,
    HlsPlaylistInfo& outInfo,
    std::atomic<bool>& abortFlag
) {
    std::string rawData;
    if (!HttpEngine::DownloadBuffer(playlistUrl, referer, cookies, rawData, abortFlag)) {
        return false;
    }

    // Convert UTF-8 rawData to wide
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, rawData.data(), (int)rawData.size(), NULL, 0);
    if (sizeNeeded <= 0) return false;
    std::wstring content(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, rawData.data(), (int)rawData.size(), &content[0], sizeNeeded);

    std::wstringstream ss(content);
    std::wstring line;
    std::getline(ss, line);
    line = TrimLine(line);

    if (line.find(L"#EXTM3U") == std::wstring::npos) {
        return false;
    }

    outInfo.qualities.clear();
    outInfo.initSegmentUrl.clear();
    outInfo.segmentUrls.clear();
    outInfo.totalDuration = 0.0;
    outInfo.isMaster = false;

    // Check if playlistUrl has an auth token in query string (e.g. access_token=, token=, hb=, etc.)
    std::wstring authTokenQuery;
    size_t qPos = playlistUrl.find(L'?');
    if (qPos != std::wstring::npos) {
        authTokenQuery = playlistUrl.substr(qPos + 1);
    }

    auto AppendAuthQuery = [&](const std::wstring& targetUrl) -> std::wstring {
        if (authTokenQuery.empty() || targetUrl.empty()) return targetUrl;
        if (targetUrl.find(L"token=") != std::wstring::npos || targetUrl.find(L"access_token=") != std::wstring::npos) {
            return targetUrl;
        }
        std::wstring res = targetUrl;
        if (res.find(L'?') != std::wstring::npos) {
            res += L"&" + authTokenQuery;
        } else {
            res += L"?" + authTokenQuery;
        }
        return res;
    };

    StreamQuality currentQuality;
    bool expectingMediaPlaylist = false;

    while (std::getline(ss, line)) {
        line = TrimLine(line);
        if (line.empty()) continue;

        if (line.rfind(L"#EXT-X-STREAM-INF:", 0) == 0) {
            outInfo.isMaster = true;
            expectingMediaPlaylist = true;
            currentQuality = StreamQuality();

            // Parse RESOLUTION
            size_t resPos = line.find(L"RESOLUTION=");
            if (resPos != std::wstring::npos) {
                size_t comma = line.find(L',', resPos);
                std::wstring resStr = line.substr(resPos + 11, (comma != std::wstring::npos ? comma - (resPos + 11) : std::wstring::npos));
                currentQuality.resolution = resStr;
            }

            // Parse BANDWIDTH
            size_t bwPos = line.find(L"BANDWIDTH=");
            if (bwPos != std::wstring::npos) {
                size_t comma = line.find(L',', bwPos);
                std::wstring bwStr = line.substr(bwPos + 10, (comma != std::wstring::npos ? comma - (bwPos + 10) : std::wstring::npos));
                try {
                    currentQuality.bandwidth = std::stoi(bwStr);
                } catch (...) {}
            }
        } else if (expectingMediaPlaylist && line[0] != L'#') {
            currentQuality.url = AppendAuthQuery(ResolveUrl(playlistUrl, line));
            if (currentQuality.resolution.empty()) {
                currentQuality.resolution = std::to_wstring(currentQuality.bandwidth / 1000) + L" kbps";
            }
            outInfo.qualities.push_back(currentQuality);
            expectingMediaPlaylist = false;
        } else if (line.rfind(L"#EXT-X-MAP:", 0) == 0) {
            // Fragmented MP4 / CMAF initialization segment
            size_t uriPos = line.find(L"URI=\"");
            if (uriPos != std::wstring::npos) {
                uriPos += 5;
                size_t endQuote = line.find(L'\"', uriPos);
                if (endQuote != std::wstring::npos) {
                    std::wstring uri = line.substr(uriPos, endQuote - uriPos);
                    outInfo.initSegmentUrl = AppendAuthQuery(ResolveUrl(playlistUrl, uri));
                }
            } else {
                uriPos = line.find(L"URI=");
                if (uriPos != std::wstring::npos) {
                    uriPos += 4;
                    size_t comma = line.find(L',', uriPos);
                    std::wstring uri = line.substr(uriPos, comma != std::wstring::npos ? comma - uriPos : std::wstring::npos);
                    outInfo.initSegmentUrl = AppendAuthQuery(ResolveUrl(playlistUrl, uri));
                }
            }
        } else if (line.rfind(L"#EXTINF:", 0) == 0) {
            size_t comma = line.find(L',');
            std::wstring durStr = line.substr(8, comma != std::wstring::npos ? comma - 8 : std::wstring::npos);
            try {
                outInfo.totalDuration += std::stod(durStr);
            } catch (...) {}
        } else if (line[0] != L'#') {
            // Segment URL
            outInfo.segmentUrls.push_back(AppendAuthQuery(ResolveUrl(playlistUrl, line)));
        }
    }

    if (outInfo.isMaster && !outInfo.qualities.empty()) {
        // Fetch first variant to get total duration
        HlsPlaylistInfo firstVariant;
        std::atomic<bool> dummyAbort{ false };
        if (ParsePlaylist(outInfo.qualities[0].url, referer, cookies, firstVariant, dummyAbort)) {
            outInfo.totalDuration = firstVariant.totalDuration;
            for (auto& q : outInfo.qualities) {
                if (outInfo.totalDuration > 0 && q.bandwidth > 0) {
                    q.estimatedBytes = (int64_t)((double)q.bandwidth * outInfo.totalDuration / 8.0);
                }
            }
        }
    }

    return true;
}

bool HlsDownloader::DownloadHlsStream(
    const std::wstring& playlistUrl,
    const std::wstring& targetFilePath,
    const std::wstring& referer,
    const std::wstring& cookies,
    int maxWorkers,
    std::atomic<bool>& abortFlag,
    std::atomic<bool>& pauseFlag,
    HlsProgressCallback progressCallback,
    TokenRefreshCallback tokenRefresher
) {
    HlsPlaylistInfo info;
    if (!ParsePlaylist(playlistUrl, referer, cookies, info, abortFlag)) {
        return false;
    }

    // If master, select highest resolution quality
    if (info.isMaster && !info.qualities.empty()) {
        std::wstring bestUrl = info.qualities.back().url;
        int bestBandwidth = -1;
        for (const auto& q : info.qualities) {
            if (q.bandwidth > bestBandwidth) {
                bestBandwidth = q.bandwidth;
                bestUrl = q.url;
            }
        }
        if (!ParsePlaylist(bestUrl, referer, cookies, info, abortFlag)) {
            return false;
        }
    }

    if (info.segmentUrls.empty()) {
        return false;
    }

    int totalSegments = (int)info.segmentUrls.size();

    // Dedicated task segment cache directory next to target file (enables instant, lossless resume!)
    std::wstring cacheDir = targetFilePath + L".hls_parts";
    SHCreateDirectoryExW(NULL, cacheDir.c_str(), NULL);

    // Dynamic Token Management
    std::mutex tokenMutex;
    std::wstring currentToken;
    size_t tokPos = playlistUrl.find(L"access_token=");
    if (tokPos != std::wstring::npos) {
        size_t endPos = playlistUrl.find(L'&', tokPos);
        currentToken = playlistUrl.substr(tokPos + 13, (endPos != std::wstring::npos ? endPos - (tokPos + 13) : std::wstring::npos));
    } else {
        tokPos = playlistUrl.find(L"token=");
        if (tokPos != std::wstring::npos) {
            size_t endPos = playlistUrl.find(L'&', tokPos);
            currentToken = playlistUrl.substr(tokPos + 6, (endPos != std::wstring::npos ? endPos - (tokPos + 6) : std::wstring::npos));
        }
    }

    auto ReplaceOrAppendToken = [](const std::wstring& url, const std::wstring& token) -> std::wstring {
        if (token.empty() || url.empty()) return url;
        size_t tPos = url.find(L"access_token=");
        if (tPos != std::wstring::npos) {
            size_t endPos = url.find(L'&', tPos);
            std::wstring newUrl = url.substr(0, tPos + 13) + token;
            if (endPos != std::wstring::npos) newUrl += url.substr(endPos);
            return newUrl;
        }
        tPos = url.find(L"token=");
        if (tPos != std::wstring::npos) {
            size_t endPos = url.find(L'&', tPos);
            std::wstring newUrl = url.substr(0, tPos + 6) + token;
            if (endPos != std::wstring::npos) newUrl += url.substr(endPos);
            return newUrl;
        }
        if (url.find(L'?') != std::wstring::npos) {
            return url + L"&access_token=" + token;
        } else {
            return url + L"?access_token=" + token;
        }
    };

    // Download fMP4 / CMAF initialization segment if present (#EXT-X-MAP)
    std::wstring initSegFile;
    if (!info.initSegmentUrl.empty()) {
        initSegFile = cacheDir + L"\\init_segment.mp4";
        bool initAlreadyDone = false;
        HANDLE hInit = CreateFileW(initSegFile.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (hInit != INVALID_HANDLE_VALUE) {
            if (GetFileSize(hInit, NULL) > 0) initAlreadyDone = true;
            CloseHandle(hInit);
        }

        if (!initAlreadyDone) {
            bool initOk = false;
            std::string initData;
            std::wstring initUrl = info.initSegmentUrl;
            {
                std::lock_guard<std::mutex> lock(tokenMutex);
                if (!currentToken.empty()) {
                    initUrl = ReplaceOrAppendToken(initUrl, currentToken);
                }
            }

            for (int attempt = 0; attempt < 6 && !abortFlag && !pauseFlag; ++attempt) {
                int sc = 0;
                if (HttpEngine::DownloadBuffer(initUrl, referer, cookies, initData, abortFlag, L"", &sc)) {
                    initOk = true;
                    break;
                }
                if ((sc == 401 || sc == 403) && tokenRefresher) {
                    std::lock_guard<std::mutex> lock(tokenMutex);
                    std::wstring newToken;
                    if (tokenRefresher(newToken) && !newToken.empty()) {
                        currentToken = newToken;
                        initUrl = ReplaceOrAppendToken(info.initSegmentUrl, currentToken);
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1000 * (attempt + 1)));
            }

            if (initOk) {
                std::ofstream out(initSegFile, std::ios::binary | std::ios::trunc);
                if (out) {
                    out.write(initData.data(), initData.size());
                    out.close();
                } else {
                    initOk = false;
                }
            }

            if (!initOk) {
                return false;
            }
        }
    }

    std::vector<std::wstring> segFiles(totalSegments);
    for (int i = 0; i < totalSegments; ++i) {
        segFiles[i] = cacheDir + L"\\seg_" + std::to_wstring(i) + L".tmp";
    }

    // Pre-scan existing segments for instant, lossless resume
    int preCompleted = 0;
    int64_t preBytes = 0;
    std::vector<bool> alreadyDone(totalSegments, false);

    for (int i = 0; i < totalSegments; ++i) {
        HANDLE hSeg = CreateFileW(segFiles[i].c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (hSeg != INVALID_HANDLE_VALUE) {
            DWORD sz = GetFileSize(hSeg, NULL);
            CloseHandle(hSeg);
            if (sz > 0) {
                alreadyDone[i] = true;
                preCompleted++;
                preBytes += sz;
            }
        }
    }

    int workersCount = (maxWorkers > 0 && maxWorkers <= 32) ? maxWorkers : 8;
    if (workersCount > totalSegments) workersCount = totalSegments;
    if (workersCount < 1) workersCount = 1;

    std::vector<int> sliceStart(workersCount);
    std::vector<int> sliceEnd(workersCount);
    std::unique_ptr<std::atomic<int>[]> sliceCurrent(new std::atomic<int>[workersCount]);
    std::unique_ptr<std::atomic<int>[]> sliceCompleted(new std::atomic<int>[workersCount]);

    for (int w = 0; w < workersCount; ++w) {
        sliceStart[w] = (w * totalSegments) / workersCount;
        sliceEnd[w] = ((w + 1) * totalSegments) / workersCount;
        sliceCurrent[w].store(sliceStart[w]);
        int sliceDone = 0;
        for (int i = sliceStart[w]; i < sliceEnd[w]; ++i) {
            if (alreadyDone[i]) sliceDone++;
        }
        sliceCompleted[w].store(sliceDone);
    }

    std::atomic<int> completedCount{ preCompleted };
    std::atomic<int64_t> totalBytesRead{ preBytes };

    // Initial progress report for ALL workers if resuming from cache
    if (preCompleted > 0 && progressCallback) {
        for (int w = 0; w < workersCount; ++w) {
            int chunkTotal = sliceEnd[w] - sliceStart[w];
            progressCallback(w, sliceCompleted[w].load(), chunkTotal, preCompleted, totalSegments, preBytes);
        }
    }

    std::mutex retryMutex;
    std::vector<int> retryQueue;
    std::atomic<int> retryIndex{ 0 };

    auto DownloadSegment = [&](int idx, int targetChunk) -> bool {
        if (idx < 0 || idx >= totalSegments) return false;
        if (alreadyDone[idx]) return true;

        const std::wstring& baseSegUrl = info.segmentUrls[idx];
        const std::wstring& segFile = segFiles[idx];

        std::wstring segUrl = baseSegUrl;
        {
            std::lock_guard<std::mutex> lock(tokenMutex);
            if (!currentToken.empty()) {
                segUrl = ReplaceOrAppendToken(segUrl, currentToken);
            }
        }

        bool segOk = false;
        std::string segData;
        int statusCode = 0;

        for (int attempt = 0; attempt < 5 && !abortFlag && !pauseFlag; ++attempt) {
            if (HttpEngine::DownloadBuffer(segUrl, referer, cookies, segData, abortFlag, L"", &statusCode)) {
                segOk = true;
                break;
            }

            // If 401 or 403 occurs and we have a token refresher, fetch a fresh token immediately
            if ((statusCode == 401 || statusCode == 403) && tokenRefresher) {
                std::lock_guard<std::mutex> lock(tokenMutex);
                std::wstring newToken;
                if (tokenRefresher(newToken) && !newToken.empty()) {
                    currentToken = newToken;
                    segUrl = ReplaceOrAppendToken(baseSegUrl, currentToken);
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500 * (attempt + 1)));
        }

        if (segOk) {
            std::ofstream out(segFile, std::ios::binary | std::ios::trunc);
            if (out) {
                out.write(segData.data(), segData.size());
                out.close();

                alreadyDone[idx] = true;
                int chunkDone = ++sliceCompleted[targetChunk];
                int gDone = ++completedCount;
                int64_t bRead = (totalBytesRead += segData.size());

                if (progressCallback) {
                    int chunkTotal = sliceEnd[targetChunk] - sliceStart[targetChunk];
                    progressCallback(targetChunk, chunkDone, chunkTotal, gDone, totalSegments, bRead);
                }
                return true;
            }
        }
        return false;
    };

    std::vector<std::thread> workers;

    for (int w = 0; w < workersCount; ++w) {
        workers.emplace_back([&, w]() {
            // Phase 1: Download assigned slice + work-steal
            while (!abortFlag && !pauseFlag) {
                int targetChunk = w;
                int idx = sliceCurrent[w].fetch_add(1);

                if (idx >= sliceEnd[w]) {
                    // Own slice finished; steal from the slice with the most remaining work
                    int bestChunk = -1;
                    int maxRemaining = 1;
                    for (int other = 0; other < workersCount; ++other) {
                        int cur = sliceCurrent[other].load();
                        int rem = sliceEnd[other] - cur;
                        if (rem > maxRemaining) {
                            maxRemaining = rem;
                            bestChunk = other;
                        }
                    }

                    if (bestChunk >= 0) {
                        idx = sliceCurrent[bestChunk].fetch_add(1);
                        if (idx < sliceEnd[bestChunk]) {
                            targetChunk = bestChunk;
                        } else {
                            break; // No more work to steal
                        }
                    } else {
                        break; // All slices completed
                    }
                }

                if (idx < 0 || idx >= totalSegments) break;

                if (alreadyDone[idx]) {
                    continue;
                }

                if (!DownloadSegment(idx, targetChunk)) {
                    if (!abortFlag && !pauseFlag) {
                        std::lock_guard<std::mutex> lock(retryMutex);
                        retryQueue.push_back(idx);
                    }
                }
            }

            // Phase 2: Process any segments that temporarily failed
            while (!abortFlag && !pauseFlag) {
                int rIdx = -1;
                {
                    std::lock_guard<std::mutex> lock(retryMutex);
                    int cur = retryIndex.fetch_add(1);
                    if (cur < (int)retryQueue.size()) {
                        rIdx = retryQueue[cur];
                    }
                }
                if (rIdx < 0) break;

                if (!alreadyDone[rIdx]) {
                    // Find chunk that owns this segment
                    int ownerChunk = w;
                    for (int c = 0; c < workersCount; ++c) {
                        if (rIdx >= sliceStart[c] && rIdx < sliceEnd[c]) {
                            ownerChunk = c;
                            break;
                        }
                    }

                    // Retry up to 8 times with backoff
                    for (int retryAttempt = 0; retryAttempt < 8 && !abortFlag && !pauseFlag; ++retryAttempt) {
                        if (DownloadSegment(rIdx, ownerChunk)) {
                            break;
                        }
                        if (tokenRefresher) {
                            std::lock_guard<std::mutex> lock(tokenMutex);
                            std::wstring newToken;
                            if (tokenRefresher(newToken) && !newToken.empty()) {
                                currentToken = newToken;
                            }
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(1000 * (retryAttempt + 1)));
                    }
                }
            }
        });
    }

    for (auto& t : workers) {
        if (t.joinable()) t.join();
    }

    if (abortFlag || pauseFlag || completedCount.load() < totalSegments) {
        // DO NOT delete already downloaded segment files!
        // They remain safely in cacheDir for instant lossless resume!
        return false;
    }

    // Ensure target output file has .mp4 extension, never .m3u8!
    std::wstring outPath = targetFilePath;
    if (outPath.length() >= 5 && _wcsicmp(outPath.c_str() + outPath.length() - 5, L".m3u8") == 0) {
        outPath = outPath.substr(0, outPath.length() - 5) + L".mp4";
    }

    // Merge segments sequentially into target file
    HANDLE hTarget = CreateFileW(
        outPath.c_str(),
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hTarget == INVALID_HANDLE_VALUE) {
        return false;
    }

    std::vector<char> buffer(256 * 1024);

    // 1. If initialization segment exists (fMP4/CMAF), write it first at offset 0!
    if (!initSegFile.empty()) {
        std::ifstream in(initSegFile, std::ios::binary);
        if (in) {
            while (in.read(buffer.data(), buffer.size()) || in.gcount() > 0) {
                DWORD bytesWritten = 0;
                WriteFile(hTarget, buffer.data(), (DWORD)in.gcount(), &bytesWritten, NULL);
            }
            in.close();
        }
        DeleteFileW(initSegFile.c_str());
    }

    // 2. Write all media segments sequentially
    for (int i = 0; i < totalSegments; ++i) {
        std::ifstream in(segFiles[i], std::ios::binary);
        if (in) {
            while (in.read(buffer.data(), buffer.size()) || in.gcount() > 0) {
                DWORD bytesWritten = 0;
                WriteFile(hTarget, buffer.data(), (DWORD)in.gcount(), &bytesWritten, NULL);
            }
            in.close();
        }
        DeleteFileW(segFiles[i].c_str());
    }

    CloseHandle(hTarget);
    RemoveDirectoryW(cacheDir.c_str());
    return true;
}

bool HlsDownloader::IsYtDlpAvailable(std::wstring& outPath) {
    WCHAR exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);

    // 1. Same folder as app
    std::wstring localYtDlp = std::wstring(exePath) + L"\\yt-dlp.exe";
    if (PathFileExistsW(localYtDlp.c_str())) {
        outPath = localYtDlp;
        return true;
    }

    // 2. tools subfolder
    std::wstring toolsYtDlp = std::wstring(exePath) + L"\\tools\\yt-dlp.exe";
    if (PathFileExistsW(toolsYtDlp.c_str())) {
        outPath = toolsYtDlp;
        return true;
    }

    // 3. Parent's tools folder (development mode)
    WCHAR parentDir[MAX_PATH] = { 0 };
    wcscpy_s(parentDir, exePath);
    PathRemoveFileSpecW(parentDir);
    std::wstring devToolsYtDlp = std::wstring(parentDir) + L"\\tools\\yt-dlp.exe";
    if (PathFileExistsW(devToolsYtDlp.c_str())) {
        outPath = devToolsYtDlp;
        return true;
    }

    // 4. System PATH
    WCHAR sysPath[MAX_PATH] = { 0 };
    if (SearchPathW(NULL, L"yt-dlp.exe", NULL, MAX_PATH, sysPath, NULL) > 0) {
        outPath = sysPath;
        return true;
    }

    return false;
}

std::wstring HlsDownloader::FindFfmpegLocation() {
    WCHAR exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);

    // 1. App folder or tools folder
    std::wstring localFfmpeg = std::wstring(exePath) + L"\\ffmpeg.exe";
    if (PathFileExistsW(localFfmpeg.c_str())) return exePath;

    std::wstring toolsFfmpeg = std::wstring(exePath) + L"\\tools\\ffmpeg.exe";
    if (PathFileExistsW(toolsFfmpeg.c_str())) return std::wstring(exePath) + L"\\tools";

    // 2. Parent's tools folder
    WCHAR parentDir[MAX_PATH] = { 0 };
    wcscpy_s(parentDir, exePath);
    PathRemoveFileSpecW(parentDir);
    std::wstring devToolsFfmpeg = std::wstring(parentDir) + L"\\tools\\ffmpeg.exe";
    if (PathFileExistsW(devToolsFfmpeg.c_str())) return std::wstring(parentDir) + L"\\tools";

    // 3. Chocolatey installation paths
    if (PathFileExistsW(L"C:\\ProgramData\\chocolatey\\bin\\ffmpeg.exe")) {
        return L"C:\\ProgramData\\chocolatey\\bin";
    }
    if (PathFileExistsW(L"C:\\ProgramData\\chocolatey\\lib\\ffmpeg\\tools\\ffmpeg\\bin\\ffmpeg.exe")) {
        return L"C:\\ProgramData\\chocolatey\\lib\\ffmpeg\\tools\\ffmpeg\\bin";
    }

    // 4. System PATH
    WCHAR sysPath[MAX_PATH] = { 0 };
    if (SearchPathW(NULL, L"ffmpeg.exe", NULL, MAX_PATH, sysPath, NULL) > 0) {
        PathRemoveFileSpecW(sysPath);
        return sysPath;
    }

    return L"";
}

void HlsDownloader::UpdateYtDlpInBackground() {
    std::thread([]() {
        std::wstring ytDlpPath;
        if (!IsYtDlpAvailable(ytDlpPath)) return;

        std::wstring cmd = L"\"" + ytDlpPath + L"\" -U --no-warnings";

        STARTUPINFOW si = { sizeof(STARTUPINFOW) };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        PROCESS_INFORMATION pi = { 0 };
        std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
        cmdBuf.push_back(0);

        if (CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            // Wait up to 60 seconds in background
            WaitForSingleObject(pi.hProcess, 60000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }).detach();
}

static std::string WideToUtf8Helper(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), NULL, 0, NULL, NULL);
    if (size <= 0) return "";
    std::string out(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &out[0], size, NULL, NULL);
    return out;
}

static std::wstring Utf8ToWideHelper(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), NULL, 0);
    if (size <= 0) return L"";
    std::wstring out(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &out[0], size);
    return out;
}

struct TempCookieFileCleanup {
    std::wstring path;
    ~TempCookieFileCleanup() {
        if (!path.empty()) {
            DeleteFileW(path.c_str());
        }
    }
};

static std::wstring CreateTempCookieFile(const std::wstring& cookies) {
    if (cookies.empty()) return L"";

    WCHAR tempDir[MAX_PATH] = { 0 };
    GetTempPathW(MAX_PATH, tempDir);

    GUID guid;
    CoCreateGuid(&guid);
    WCHAR guidStr[64] = { 0 };
    StringFromGUID2(guid, guidStr, 64);
    std::wstring safeGuid = guidStr;
    if (!safeGuid.empty() && safeGuid.front() == L'{') safeGuid.erase(0, 1);
    if (!safeGuid.empty() && safeGuid.back() == L'}') safeGuid.pop_back();

    std::wstring cookieFilePath = std::wstring(tempDir) + L"easydl_yt_cookies_" + safeGuid + L".txt";

    std::string netscapeContent;

    if (cookies.find(L"# Netscape") != std::wstring::npos) {
        netscapeContent = WideToUtf8Helper(cookies);
    } else {
        netscapeContent = "# Netscape HTTP Cookie File\n# Generated by EasyDownload\n\n";
        int64_t farFuture = (int64_t)time(nullptr) + 86400 * 365;

        std::wstringstream ss(cookies);
        std::wstring token;
        while (std::getline(ss, token, L';')) {
            size_t first = token.find_first_not_of(L" \t\r\n");
            if (first == std::wstring::npos) continue;
            size_t last = token.find_last_not_of(L" \t\r\n");
            token = token.substr(first, (last - first + 1));

            size_t eqPos = token.find(L'=');
            if (eqPos == std::wstring::npos || eqPos == 0) continue;

            std::wstring key = token.substr(0, eqPos);
            std::wstring val = token.substr(eqPos + 1);

            std::string uKey = WideToUtf8Helper(key);
            std::string uVal = WideToUtf8Helper(val);

            netscapeContent += ".youtube.com\tTRUE\t/\tTRUE\t" + std::to_string(farFuture) + "\t" + uKey + "\t" + uVal + "\n";
            netscapeContent += ".google.com\tTRUE\t/\tTRUE\t" + std::to_string(farFuture) + "\t" + uKey + "\t" + uVal + "\n";
        }
    }

    if (netscapeContent.empty()) return L"";

    HANDLE hFile = CreateFileW(cookieFilePath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return L"";

    DWORD bytesWritten = 0;
    WriteFile(hFile, netscapeContent.data(), (DWORD)netscapeContent.size(), &bytesWritten, NULL);
    CloseHandle(hFile);

    return cookieFilePath;
}

std::wstring HlsDownloader::CleanYouTubeUrl(const std::wstring& url) {
    if (url.empty()) return url;

    std::wstring lower = url;
    for (auto& c : lower) c = (wchar_t)towlower(c);

    if (lower.find(L"youtube.com") == std::wstring::npos && lower.find(L"youtu.be") == std::wstring::npos) {
        return url;
    }

    // 1. Shorts: youtube.com/shorts/VIDEO_ID
    size_t shortsPos = lower.find(L"/shorts/");
    if (shortsPos != std::wstring::npos) {
        size_t idStart = shortsPos + 8;
        size_t idEnd = url.find_first_of(L"?/&#", idStart);
        std::wstring vid = (idEnd == std::wstring::npos) ? url.substr(idStart) : url.substr(idStart, idEnd - idStart);
        if (!vid.empty()) {
            return L"https://www.youtube.com/shorts/" + vid;
        }
    }

    // 2. youtu.be/VIDEO_ID
    size_t youtuBePos = lower.find(L"youtu.be/");
    if (youtuBePos != std::wstring::npos) {
        size_t idStart = youtuBePos + 9;
        size_t idEnd = url.find_first_of(L"?/&#", idStart);
        std::wstring vid = (idEnd == std::wstring::npos) ? url.substr(idStart) : url.substr(idStart, idEnd - idStart);
        if (!vid.empty()) {
            return L"https://www.youtube.com/watch?v=" + vid;
        }
    }

    // 3. youtube.com/watch?v=VIDEO_ID (strip &list=..., &start_radio=..., &index=..., etc.)
    size_t vPos = std::wstring::npos;
    size_t qv = lower.find(L"?v=");
    size_t av = lower.find(L"&v=");
    if (qv != std::wstring::npos) vPos = qv + 3;
    else if (av != std::wstring::npos) vPos = av + 3;

    if (vPos != std::wstring::npos) {
        size_t idEnd = url.find_first_of(L"&#", vPos);
        std::wstring vid = (idEnd == std::wstring::npos) ? url.substr(vPos) : url.substr(vPos, idEnd - vPos);
        if (!vid.empty()) {
            return L"https://www.youtube.com/watch?v=" + vid;
        }
    }

    return url;
}

std::wstring HlsDownloader::GetVideoTitleWithYtDlp(const std::wstring& videoPageUrl, const std::wstring& cookies) {
    std::wstring ytDlpPath;
    if (!IsYtDlpAvailable(ytDlpPath)) return L"";

    std::wstring cleanUrl = CleanYouTubeUrl(videoPageUrl);

    auto tryGetTitle = [&](const std::wstring& cookieStr) -> std::wstring {
        std::wstring cookieFilePath = CreateTempCookieFile(cookieStr);
        TempCookieFileCleanup cookieCleanup{ cookieFilePath };

        std::wstring cookieOpt = L"";
        if (!cookieFilePath.empty()) {
            cookieOpt = L" --cookies \"" + cookieFilePath + L"\"";
        }

        // Use --encoding utf-8 --no-warnings and --print title to get ONLY the video title cleanly in UTF-8
        std::wstring cmd = L"\"" + ytDlpPath + L"\" --encoding utf-8 --no-warnings --no-playlist" + cookieOpt + L" --print title \"" + cleanUrl + L"\"";

        SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
        HANDLE hReadPipe = NULL, hWritePipe = NULL;
        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return L"";
        SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si = { sizeof(STARTUPINFOW) };
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.hStdOutput = hWritePipe;
        si.hStdError = INVALID_HANDLE_VALUE; // Do NOT pipe stderr into title output!
        si.wShowWindow = SW_HIDE;

        PROCESS_INFORMATION pi = { 0 };
        std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
        cmdBuf.push_back(0);

        if (!CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(hReadPipe);
            CloseHandle(hWritePipe);
            return L"";
        }

        CloseHandle(hWritePipe);

        std::string rawOutput;
        char buffer[512] = { 0 };
        DWORD bytesRead = 0;
        while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            rawOutput.append(buffer, bytesRead);
        }

        WaitForSingleObject(pi.hProcess, 15000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hReadPipe);

        if (rawOutput.empty()) return L"";
        int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, rawOutput.data(), (int)rawOutput.size(), NULL, 0);
        if (sizeNeeded <= 0) return L"";
        std::wstring title(sizeNeeded, 0);
        MultiByteToWideChar(CP_UTF8, 0, rawOutput.data(), (int)rawOutput.size(), &title[0], sizeNeeded);

        // Trim trailing whitespace / newlines
        while (!title.empty() && (title.back() == L'\r' || title.back() == L'\n' || title.back() == L' ')) {
            title.pop_back();
        }
        // Take first line if multiple
        size_t eol = title.find_first_of(L"\r\n");
        if (eol != std::wstring::npos) {
            title = title.substr(0, eol);
        }

        // Ignore WARNING/ERROR if any leaked through
        if (title.find(L"WARNING:") != std::wstring::npos || title.find(L"ERROR:") != std::wstring::npos) {
            return L"";
        }

        return HttpEngine::SanitizeFilename(title, 80);
    };

    std::wstring result = tryGetTitle(cookies);
    if (result.empty() && !cookies.empty()) {
        // Fallback: retry without cookies if cookie-based title retrieval failed
        result = tryGetTitle(L"");
    }
    return result;
}

static int64_t ParseByteSizeHelper(const std::string& str) {
    try {
        size_t start = 0;
        while (start < str.size() && (str[start] == ' ' || str[start] == '~' || str[start] == '\t')) start++;
        if (start >= str.size()) return 0;
        size_t idx = 0;
        double val = std::stod(str.substr(start), &idx);
        std::string unit = str.substr(start + idx);
        while (!unit.empty() && (unit.front() == ' ' || unit.front() == '\t')) unit.erase(0, 1);
        if (unit.find("GiB") != std::string::npos || unit.find("GB") != std::string::npos) {
            return (int64_t)(val * 1024.0 * 1024.0 * 1024.0);
        } else if (unit.find("MiB") != std::string::npos || unit.find("MB") != std::string::npos) {
            return (int64_t)(val * 1024.0 * 1024.0);
        } else if (unit.find("KiB") != std::string::npos || unit.find("KB") != std::string::npos) {
            return (int64_t)(val * 1024.0);
        } else {
            return (int64_t)val;
        }
    } catch (...) {
        return 0;
    }
}

static bool DownloadWithYtDlpSingleAttempt(
    const std::wstring& ytDlpPath,
    const std::wstring& ffmpegOpt,
    const std::wstring& cleanUrl,
    const std::wstring& outPath,
    const std::wstring& cookies,
    std::atomic<bool>& abortFlag,
    HlsDownloader::YtDlpProgressCallback progressCallback,
    std::wstring* pOutError,
    bool isAudioOnly = false,
    const std::wstring& audioFormat = L"mp3"
) {
    std::wstring cookieFilePath = CreateTempCookieFile(cookies);
    TempCookieFileCleanup cookieCleanup{ cookieFilePath };

    std::wstring cookieOpt = L"";
    if (!cookieFilePath.empty()) {
        cookieOpt = L" --cookies \"" + cookieFilePath + L"\"";
    }

    std::wstring formatOpt;
    if (isAudioOnly) {
        std::wstring fmt = audioFormat.empty() ? L"mp3" : audioFormat;
        for (auto& c : fmt) c = (wchar_t)towlower(c);
        if (fmt == L"m4a") {
            formatOpt = L"-f \"ba[ext=m4a]/ba/best\" -x --audio-format m4a";
        } else {
            // High quality MP3 audio extraction (VBR 0 / ~320kbps)
            formatOpt = L"-x --audio-format mp3 --audio-quality 0";
        }
    } else {
        formatOpt = L"-f \"bv*[ext=mp4]+ba[ext=m4a]/b[ext=mp4]/b/best\" --merge-output-format mp4";
    }

    std::wstring cmd = L"\"" + ytDlpPath + L"\" --encoding utf-8 --no-playlist --newline --no-warnings "
                       + formatOpt
                       + ffmpegOpt
                       + cookieOpt +
                       L" -o \"" + outPath + L"\" \"" + cleanUrl + L"\"";

    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    HANDLE hReadPipe = NULL, hWritePipe = NULL;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return false;
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe; // Progress and merge info may go to stdout/stderr
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = { 0 };
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(0);

    if (!CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return false;
    }

    CloseHandle(hWritePipe);

    std::string lineBuf;
    std::string capturedError;
    char buffer[512] = { 0 };
    DWORD bytesRead = 0;
    int streamIndex = 0; // 0: Video stream, 1: Audio stream, 2: Muxing / Merger
    int destinationCount = 0;
    bool isDualStream = true;
    int lastRawPct = 0;
    int lastReportedOverallPct = 0;
    int64_t videoTotalBytes = 0;

    while (!abortFlag && ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = 0;
        lineBuf.append(buffer, bytesRead);

        size_t newlinePos;
        while ((newlinePos = lineBuf.find('\n')) != std::string::npos) {
            std::string line = lineBuf.substr(0, newlinePos);
            lineBuf.erase(0, newlinePos + 1);

            // Capture yt-dlp error line
            if (line.find("ERROR:") != std::string::npos) {
                capturedError = line;
            }

            // Detect format combination: e.g. "Downloading 1 format(s): 616+140"
            if (line.find("Downloading 1 format(s):") != std::string::npos) {
                isDualStream = (line.find('+') != std::string::npos);
            }

            // Detect merger or audio transition
            if (line.find("[Merger]") != std::string::npos || line.find("Merging formats") != std::string::npos) {
                streamIndex = 2;
                if (lastReportedOverallPct < 99) lastReportedOverallPct = 99;
                if (progressCallback) {
                    progressCallback(99, videoTotalBytes, videoTotalBytes, 0);
                }
            } else if (line.find("[download] Destination:") != std::string::npos) {
                destinationCount++;
                if (line.find(".m4a") != std::string::npos || line.find(".opus") != std::string::npos ||
                    line.find(".aac") != std::string::npos || line.find(".mp3") != std::string::npos) {
                    streamIndex = 1; // Explicit audio destination
                } else if (destinationCount >= 2) {
                    streamIndex = 1;
                } else {
                    streamIndex = 0;
                }
            }

            // Skip fragment 0 (init header like 712 bytes) which always outputs 100.0% prematurely
            if (line.find("frag 0/") != std::string::npos || line.find("(frag 0/") != std::string::npos) {
                continue;
            }

            size_t pctPos = line.find('%');
            if (pctPos != std::string::npos && pctPos > 3) {
                size_t start = line.rfind(' ', pctPos - 1);
                if (start != std::string::npos) {
                    try {
                        float pct = std::stof(line.substr(start + 1, pctPos - start - 1));
                        int64_t totalBytes = 0;
                        int64_t speedBytes = 0;

                        // Parse total bytes from " of  128.52MiB" or " of ~ 23.61MiB"
                        size_t ofPos = line.find(" of ");
                        if (ofPos != std::string::npos) {
                            size_t ofStart = ofPos + 4;
                            while (ofStart < line.size() && (line[ofStart] == ' ' || line[ofStart] == '~')) ofStart++;
                            size_t ofEnd = line.find(' ', ofStart);
                            if (ofEnd != std::string::npos) {
                                totalBytes = ParseByteSizeHelper(line.substr(ofStart, ofEnd - ofStart));
                            }
                        }

                        // Parse speed from " at    5.42MiB/s"
                        size_t atPos = line.find(" at ");
                        if (atPos != std::string::npos) {
                            size_t atStart = atPos + 4;
                            while (atStart < line.size() && line[atStart] == ' ') atStart++;
                            size_t atEnd = line.find(' ', atStart);
                            if (atEnd != std::string::npos) {
                                std::string speedStr = line.substr(atStart, atEnd - atStart);
                                if (speedStr.size() >= 2 && speedStr.substr(speedStr.size() - 2) == "/s") {
                                    speedStr = speedStr.substr(0, speedStr.size() - 2);
                                }
                                speedBytes = ParseByteSizeHelper(speedStr);
                            }
                        }

                        lastRawPct = (int)pct;

                        if (streamIndex == 0 && totalBytes > 0) {
                            videoTotalBytes = totalBytes;
                        }

                        int overallPct = 0;
                        if (!isDualStream) {
                            overallPct = (int)pct;
                        } else {
                            if (streamIndex == 0) {
                                overallPct = (int)(pct * 0.90f); // 0% ~ 90% (Video)
                            } else if (streamIndex == 1) {
                                overallPct = 90 + (int)(pct * 0.08f); // 90% ~ 98% (Audio)
                            } else {
                                overallPct = 99; // 99% (Muxing)
                            }
                        }

                        if (overallPct < lastReportedOverallPct) {
                            overallPct = lastReportedOverallPct;
                        }
                        lastReportedOverallPct = overallPct;

                        int64_t downBytes = (totalBytes > 0) ? (int64_t)(totalBytes * (pct / 100.0)) : 0;
                        int64_t reportDown = downBytes;
                        int64_t reportTotal = totalBytes;
                        if (streamIndex == 1 && videoTotalBytes > 0) {
                            reportTotal = videoTotalBytes + totalBytes;
                            reportDown = videoTotalBytes + downBytes;
                        }

                        if (progressCallback) {
                            progressCallback(overallPct, reportDown, reportTotal, speedBytes);
                        }
                    } catch (...) {}
                }
            }
        }
    }

    if (abortFlag) {
        TerminateProcess(pi.hProcess, 1);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hReadPipe);

    if (exitCode == 0 && progressCallback) {
        int64_t finalBytes = videoTotalBytes > 0 ? videoTotalBytes : 0;
        progressCallback(100, finalBytes, finalBytes, 0);
    } else if (exitCode != 0 && pOutError) {
        if (!capturedError.empty()) {
            if (capturedError.find("Join this channel") != std::string::npos ||
                capturedError.find("members-only") != std::string::npos) {
                *pOutError = L"채널 멤버십 전용 동영상입니다. Chrome에서 해당 채널 회원으로 가입된 계정으로 로그인한 상태에서 다운로드해 주세요.";
            } else if (capturedError.find("Sign in to confirm your age") != std::string::npos) {
                *pOutError = L"연령 제한 동영상입니다. Chrome에서 성인 인증된 Google 계정으로 로그인한 상태에서 다운로드해 주세요.";
            } else if (capturedError.find("Private video") != std::string::npos) {
                *pOutError = L"비공개 동영상입니다. 동영상 소유자로부터 접근 권한이 부여된 Google 계정으로 로그인해야 합니다.";
            } else {
                size_t errPos = capturedError.find("ERROR: ");
                std::string cleanErr = (errPos != std::string::npos) ? capturedError.substr(errPos + 7) : capturedError;
                *pOutError = Utf8ToWideHelper(cleanErr);
            }
        } else {
            *pOutError = L"YouTube 다운로드 실패 (yt-dlp 실행 오류 또는 차단)";
        }
    }

    return exitCode == 0;
}

bool HlsDownloader::DownloadWithYtDlp(
    const std::wstring& videoPageUrl,
    const std::wstring& targetFilePath,
    std::atomic<bool>& abortFlag,
    YtDlpProgressCallback progressCallback,
    const std::wstring& cookies,
    std::wstring* pOutError,
    bool isAudioOnly,
    const std::wstring& audioFormat
) {
    std::wstring ytDlpPath;
    if (!IsYtDlpAvailable(ytDlpPath)) {
        if (pOutError) *pOutError = L"yt-dlp 실행 파일을 찾을 수 없습니다.";
        return false;
    }

    std::wstring cleanUrl = CleanYouTubeUrl(videoPageUrl);

    std::wstring outPath = targetFilePath;
    if (isAudioOnly) {
        std::wstring targetExt = audioFormat.empty() ? L".mp3" : (L"." + audioFormat);
        size_t dotPos = outPath.find_last_of(L".\\/");
        if (dotPos != std::wstring::npos && outPath[dotPos] == L'.') {
            outPath = outPath.substr(0, dotPos) + targetExt;
        } else {
            outPath += targetExt;
        }
    } else {
        if (outPath.length() >= 5 && _wcsicmp(outPath.c_str() + outPath.length() - 5, L".m3u8") == 0) {
            outPath = outPath.substr(0, outPath.length() - 5) + L".mp4";
        }
    }

    std::wstring ffmpegDir = FindFfmpegLocation();
    std::wstring ffmpegOpt = L"";
    if (!ffmpegDir.empty()) {
        ffmpegOpt = L" --ffmpeg-location \"" + ffmpegDir + L"\"";
    }

    bool ok = DownloadWithYtDlpSingleAttempt(
        ytDlpPath, ffmpegOpt, cleanUrl, outPath, cookies, abortFlag, progressCallback, pOutError, isAudioOnly, audioFormat
    );

    // Fallback: If failed with cookies, retry once without cookies for public videos
    if (!ok && !abortFlag && !cookies.empty()) {
        if (pOutError) pOutError->clear();
        ok = DownloadWithYtDlpSingleAttempt(
            ytDlpPath, ffmpegOpt, cleanUrl, outPath, L"", abortFlag, progressCallback, pOutError, isAudioOnly, audioFormat
        );
    }

    return ok;
}

} // namespace EasyDownload
