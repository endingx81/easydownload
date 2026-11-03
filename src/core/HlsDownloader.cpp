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
    std::function<void(int completedSegments, int totalSegments, int64_t bytesRead)> progressCallback
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

    std::vector<std::wstring> segFiles(totalSegments);
    for (int i = 0; i < totalSegments; ++i) {
        segFiles[i] = tempDir + L"\\seg_" + std::to_wstring(i) + L".ts";
    }

    std::atomic<int> nextIndex{ 0 };
    std::atomic<int> completedCount{ 0 };
    std::atomic<int64_t> totalBytesRead{ 0 };
    std::atomic<bool> workerFailed{ false };

    int workersCount = (maxWorkers > 0 && maxWorkers <= 32) ? maxWorkers : 8;
    std::vector<std::thread> workers;

    for (int w = 0; w < workersCount; ++w) {
        workers.emplace_back([&]() {
            while (!abortFlag && !pauseFlag && !workerFailed) {
                int idx = nextIndex++;
                if (idx >= totalSegments) break;

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

                        completedCount++;
                        totalBytesRead += segData.size();
                        if (progressCallback) {
                            progressCallback(completedCount.load(), totalSegments, totalBytesRead.load());
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
        for (const auto& sf : segFiles) {
            DeleteFileW(sf.c_str());
        }
        RemoveDirectoryW(tempDir.c_str());
        return false;
    }

    // Merge segments sequentially into target file
    HANDLE hTarget = CreateFileW(
        targetFilePath.c_str(),
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hTarget == INVALID_HANDLE_VALUE) {
        for (const auto& sf : segFiles) {
            DeleteFileW(sf.c_str());
        }
        RemoveDirectoryW(tempDir.c_str());
        return false;
    }

    std::vector<char> buffer(256 * 1024);
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
    // 1. Check local app folder
    WCHAR exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    std::wstring localYtDlp = std::wstring(exePath) + L"\\yt-dlp.exe";
    if (PathFileExistsW(localYtDlp.c_str())) {
        outPath = localYtDlp;
        return true;
    }

    // 2. Check PATH
    WCHAR sysPath[MAX_PATH] = { 0 };
    if (SearchPathW(NULL, L"yt-dlp.exe", NULL, MAX_PATH, sysPath, NULL) > 0) {
        outPath = sysPath;
        return true;
    }

    return false;
}

bool HlsDownloader::DownloadWithYtDlp(
    const std::wstring& videoPageUrl,
    const std::wstring& targetFilePath,
    std::atomic<bool>& abortFlag,
    std::function<void(int percent, const std::wstring& speedStr)> progressCallback
) {
    std::wstring ytDlpPath;
    if (!IsYtDlpAvailable(ytDlpPath)) return false;

    std::wstring cmd = L"\"" + ytDlpPath + L"\" --no-playlist -o \"" + targetFilePath + L"\" \"" + videoPageUrl + L"\"";

    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    HANDLE hReadPipe = NULL, hWritePipe = NULL;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return false;
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
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

    char buffer[512] = { 0 };
    DWORD bytesRead = 0;
    while (!abortFlag && ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = 0;
        std::string text(buffer);
        // Look for [download]  12.5% of ~ 50.00MiB at 4.25MiB/s
        size_t pctPos = text.find("%");
        if (pctPos != std::string::npos && pctPos > 3) {
            size_t start = text.rfind(" ", pctPos - 1);
            if (start != std::string::npos) {
                try {
                    float pct = std::stof(text.substr(start + 1, pctPos - start - 1));
                    if (progressCallback) {
                        progressCallback((int)pct, L"yt-dlp stream");
                    }
                } catch (...) {}
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

    return exitCode == 0;
}

} // namespace EasyDownload
