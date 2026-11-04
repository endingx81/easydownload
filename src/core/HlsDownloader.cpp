#include "HlsDownloader.h"
#include "HttpEngine.h"
#include <sstream>
#include <iostream>
#include <fstream>
#include <thread>
#include <mutex>
#include <shlwapi.h>
#include <shlobj.h>

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
            currentQuality.url = ResolveUrl(playlistUrl, line);
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
                    outInfo.initSegmentUrl = ResolveUrl(playlistUrl, uri);
                }
            } else {
                uriPos = line.find(L"URI=");
                if (uriPos != std::wstring::npos) {
                    uriPos += 4;
                    size_t comma = line.find(L',', uriPos);
                    std::wstring uri = line.substr(uriPos, comma != std::wstring::npos ? comma - uriPos : std::wstring::npos);
                    outInfo.initSegmentUrl = ResolveUrl(playlistUrl, uri);
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
            outInfo.segmentUrls.push_back(ResolveUrl(playlistUrl, line));
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
    HlsProgressCallback progressCallback
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

    // Create temp directory for segments
    WCHAR tempPath[MAX_PATH] = { 0 };
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring tempDir = std::wstring(tempPath) + L"EasyDownload_HLS_" + std::to_wstring(GetTickCount64());
    SHCreateDirectoryExW(NULL, tempDir.c_str(), NULL);

    // Download fMP4 / CMAF initialization segment if present (#EXT-X-MAP)
    std::wstring initSegFile;
    if (!info.initSegmentUrl.empty()) {
        initSegFile = tempDir + L"\\init_segment.mp4";
        bool initOk = false;
        std::string initData;
        for (int attempt = 0; attempt < 3 && !abortFlag && !pauseFlag; ++attempt) {
            if (HttpEngine::DownloadBuffer(info.initSegmentUrl, referer, cookies, initData, abortFlag)) {
                initOk = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(400 * (attempt + 1)));
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
            RemoveDirectoryW(tempDir.c_str());
            return false;
        }
    }

    std::vector<std::wstring> segFiles(totalSegments);
    for (int i = 0; i < totalSegments; ++i) {
        segFiles[i] = tempDir + L"\\seg_" + std::to_wstring(i) + L".tmp";
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
        sliceCompleted[w].store(0);
    }

    std::atomic<int> completedCount{ 0 };
    std::atomic<int64_t> totalBytesRead{ 0 };
    std::atomic<bool> workerFailed{ false };

    std::vector<std::thread> workers;

    for (int w = 0; w < workersCount; ++w) {
        workers.emplace_back([&, w]() {
            while (!abortFlag && !pauseFlag && !workerFailed) {
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

                const std::wstring& segUrl = info.segmentUrls[idx];
                const std::wstring& segFile = segFiles[idx];

                bool segOk = false;
                std::string segData;
                for (int attempt = 0; attempt < 3 && !abortFlag && !pauseFlag; ++attempt) {
                    if (HttpEngine::DownloadBuffer(segUrl, referer, cookies, segData, abortFlag)) {
                        segOk = true;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(400 * (attempt + 1)));
                }

                if (segOk) {
                    std::ofstream out(segFile, std::ios::binary | std::ios::trunc);
                    if (out) {
                        out.write(segData.data(), segData.size());
                        out.close();

                        int chunkDone = ++sliceCompleted[targetChunk];
                        int gDone = ++completedCount;
                        int64_t bRead = (totalBytesRead += segData.size());

                        if (progressCallback) {
                            int chunkTotal = sliceEnd[targetChunk] - sliceStart[targetChunk];
                            progressCallback(targetChunk, chunkDone, chunkTotal, gDone, totalSegments, bRead);
                        }
                    } else {
                        workerFailed = true;
                        break;
                    }
                } else {
                    if (!abortFlag && !pauseFlag) {
                        workerFailed = true;
                    }
                    break;
                }
            }
        });
    }

    for (auto& t : workers) {
        if (t.joinable()) t.join();
    }

    if (abortFlag || pauseFlag || workerFailed || completedCount.load() < totalSegments) {
        // Cleanup partial files
        if (!initSegFile.empty()) {
            DeleteFileW(initSegFile.c_str());
        }
        for (const auto& sf : segFiles) {
            DeleteFileW(sf.c_str());
        }
        RemoveDirectoryW(tempDir.c_str());
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
        if (!initSegFile.empty()) {
            DeleteFileW(initSegFile.c_str());
        }
        for (const auto& sf : segFiles) {
            DeleteFileW(sf.c_str());
        }
        RemoveDirectoryW(tempDir.c_str());
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
    RemoveDirectoryW(tempDir.c_str());
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

std::wstring HlsDownloader::GetVideoTitleWithYtDlp(const std::wstring& videoPageUrl) {
    std::wstring ytDlpPath;
    if (!IsYtDlpAvailable(ytDlpPath)) return L"";

    // Use --encoding utf-8 --no-warnings and --print title to get ONLY the video title cleanly in UTF-8
    std::wstring cmd = L"\"" + ytDlpPath + L"\" --encoding utf-8 --no-warnings --no-playlist --print title \"" + videoPageUrl + L"\"";

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

bool HlsDownloader::DownloadWithYtDlp(
    const std::wstring& videoPageUrl,
    const std::wstring& targetFilePath,
    std::atomic<bool>& abortFlag,
    YtDlpProgressCallback progressCallback
) {
    std::wstring ytDlpPath;
    if (!IsYtDlpAvailable(ytDlpPath)) return false;

    std::wstring outPath = targetFilePath;
    if (outPath.length() >= 5 && _wcsicmp(outPath.c_str() + outPath.length() - 5, L".m3u8") == 0) {
        outPath = outPath.substr(0, outPath.length() - 5) + L".mp4";
    }

    std::wstring ffmpegDir = FindFfmpegLocation();
    std::wstring ffmpegOpt = L"";
    if (!ffmpegDir.empty()) {
        ffmpegOpt = L" --ffmpeg-location \"" + ffmpegDir + L"\"";
    }

    std::wstring cmd = L"\"" + ytDlpPath + L"\" --encoding utf-8 --no-playlist --newline --no-warnings "
                       L"-f \"bv*[ext=mp4]+ba[ext=m4a]/b[ext=mp4]/b/best\" "
                       L"--merge-output-format mp4"
                       + ffmpegOpt +
                       L" -o \"" + outPath + L"\" \"" + videoPageUrl + L"\"";

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
    }

    return exitCode == 0;
}

} // namespace EasyDownload
