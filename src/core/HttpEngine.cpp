#include "HttpEngine.h"
#include <algorithm>
#include <vector>
#include <iostream>

#pragma comment(lib, "winhttp.lib")

namespace EasyDownload {

static const WCHAR* DEFAULT_USER_AGENT = L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36";

std::wstring HttpEngine::SanitizeFilename(const std::wstring& rawName, size_t maxLen) {
    std::wstring clean = rawName;
    const std::wstring invalidChars = L"\\/:*?\"<>|";
    for (auto& ch : clean) {
        if (invalidChars.find(ch) != std::wstring::npos || (ch >= 0 && ch < 32)) {
            ch = L'_';
        }
    }
    // Trim spaces and dots at the end
    while (!clean.empty() && (clean.back() == L' ' || clean.back() == L'.')) {
        clean.pop_back();
    }
    if (clean.empty()) clean = L"download_file";

    // Enforce maxLen on base filename if requested (preserving quality tag and extension)
    if (maxLen > 0 && clean.length() > maxLen) {
        size_t dot = clean.find_last_of(L'.');
        std::wstring ext = (dot != std::wstring::npos && dot > 0) ? clean.substr(dot) : L"";
        std::wstring base = (dot != std::wstring::npos && dot > 0) ? clean.substr(0, dot) : clean;

        // Separate quality suffix if present (e.g. [1080p])
        std::wstring qSuffix = L"";
        size_t qOpen = base.find_last_of(L'[');
        size_t qClose = base.find_last_of(L']');
        if (qOpen != std::wstring::npos && qClose != std::wstring::npos && qClose > qOpen) {
            qSuffix = base.substr(qOpen);
            base = base.substr(0, qOpen);
            while (!base.empty() && (base.back() == L' ' || base.back() == L'_')) base.pop_back();
        }

        if (base.length() > maxLen) {
            base = base.substr(0, maxLen);
            while (!base.empty() && (base.back() == L' ' || base.back() == L'_')) base.pop_back();
        }
        clean = base + (qSuffix.empty() ? L"" : L" " + qSuffix) + ext;
    }

    return clean;
}

bool HttpEngine::IsGenericFilename(const std::wstring& name) {
    if (name.empty()) return true;

    // Remove extension for checking
    std::wstring base = name;
    size_t dot = base.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        base = base.substr(0, dot);
    }
    // Convert to lowercase
    std::wstring lower = base;
    for (auto& c : lower) c = towlower(c);

    // Trim whitespace and underscores
    size_t start = lower.find_first_not_of(L" _-");
    size_t end = lower.find_last_not_of(L" _-");
    if (start == std::wstring::npos) return true;
    lower = lower.substr(start, end - start + 1);

    // Exact generic names
    static const std::vector<std::wstring> genericExact = {
        L"index", L"master", L"playlist", L"chunklist", L"video", L"stream",
        L"videoplayback", L"download", L"download_file", L"video_stream",
        L"동영상", L"웹 비디오", L"웹_비디오", L"동영상 스트림", L"video_download",
        L"player", L"embed", L"media", L"output", L"manifest"
    };
    for (const auto& g : genericExact) {
        if (lower == g) return true;
    }

    // Prefixes like "index-", "master-", "playlist-", "chunklist-", "stream-"
    static const std::vector<std::wstring> genericPrefixes = {
        L"index-", L"index_", L"master-", L"master_", L"playlist-", L"playlist_",
        L"chunklist-", L"chunklist_", L"stream-", L"stream_", L"seg-", L"segment-"
    };
    for (const auto& p : genericPrefixes) {
        if (lower.rfind(p, 0) == 0) return true;
    }

    return false;
}

std::wstring HttpEngine::ExtractTitleFromRefererUrl(const std::wstring& refererUrl) {
    if (refererUrl.empty()) return L"";

    URL_COMPONENTS urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;

    if (!WinHttpCrackUrl(refererUrl.c_str(), (DWORD)refererUrl.length(), 0, &urlComp)) {
        return L"";
    }

    std::wstring host = (urlComp.lpszHostName && urlComp.dwHostNameLength > 0)
        ? std::wstring(urlComp.lpszHostName, urlComp.dwHostNameLength) : L"";
    std::wstring path = (urlComp.lpszUrlPath && urlComp.dwUrlPathLength > 0)
        ? std::wstring(urlComp.lpszUrlPath, urlComp.dwUrlPathLength) : L"";

    // Clean host (remove www.)
    if (host.rfind(L"www.", 0) == 0) host = host.substr(4);
    size_t dotPos = host.find(L'.');
    std::wstring siteName = (dotPos != std::wstring::npos) ? host.substr(0, dotPos) : host;

    // Clean path: remove query, get last segment
    size_t q = path.find(L'?');
    if (q != std::wstring::npos) path = path.substr(0, q);
    size_t hash = path.find(L'#');
    if (hash != std::wstring::npos) path = path.substr(0, hash);

    // Strip trailing slashes
    while (!path.empty() && (path.back() == L'/' || path.back() == L'\\')) {
        path.pop_back();
    }

    size_t slashPos = path.find_last_of(L"/\\");
    std::wstring lastSegment = (slashPos != std::wstring::npos) ? path.substr(slashPos + 1) : path;

    // Remove .html, .htm, .php, .jsp etc.
    static const std::vector<std::wstring> exts = { L".html", L".htm", L".php", L".jsp", L".asp", L".aspx" };
    for (const auto& ext : exts) {
        if (lastSegment.length() >= ext.length() &&
            _wcsicmp(lastSegment.c_str() + lastSegment.length() - ext.length(), ext.c_str()) == 0) {
            lastSegment = lastSegment.substr(0, lastSegment.length() - ext.length());
            break;
        }
    }

    std::wstring title;
    if (!lastSegment.empty() && lastSegment != L"index" && lastSegment != L"watch" && lastSegment != L"video") {
        title = (!siteName.empty() ? siteName + L"_" : L"") + lastSegment;
    } else if (!siteName.empty()) {
        title = siteName + L"_video";
    }

    return SanitizeFilename(title);
}

std::wstring HttpEngine::ExtractFilenameFromUrl(const std::wstring& url) {
    URL_COMPONENTS urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwUrlPathLength = (DWORD)-1;

    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.length(), 0, &urlComp)) {
        return L"download.bin";
    }

    if (urlComp.lpszUrlPath && urlComp.dwUrlPathLength > 0) {
        std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
        // Remove query parameters
        size_t qPos = path.find(L'?');
        if (qPos != std::wstring::npos) path = path.substr(0, qPos);
        size_t hPos = path.find(L'#');
        if (hPos != std::wstring::npos) path = path.substr(0, hPos);

        size_t slashPos = path.find_last_of(L"/\\");
        if (slashPos != std::wstring::npos && slashPos + 1 < path.length()) {
            std::wstring fname = path.substr(slashPos + 1);
            if (!fname.empty()) return SanitizeFilename(fname);
        }
    }
    return L"download.bin";
}

std::wstring HttpEngine::ExtractFilenameFromContentDisposition(const std::wstring& cdHeader) {
    // Looks for filename="xyz" or filename*=UTF-8''xyz
    size_t utfPos = cdHeader.find(L"filename*=");
    if (utfPos != std::wstring::npos) {
        std::wstring sub = cdHeader.substr(utfPos + 10);
        size_t quoteStart = sub.find(L"''");
        if (quoteStart != std::wstring::npos) {
            std::wstring encoded = sub.substr(quoteStart + 2);
            size_t semi = encoded.find(L';');
            if (semi != std::wstring::npos) encoded = encoded.substr(0, semi);
            // URL decode encoded
            std::string utf8Str;
            for (size_t i = 0; i < encoded.length(); ++i) {
                if (encoded[i] == L'%' && i + 2 < encoded.length()) {
                    wchar_t hex[3] = { encoded[i + 1], encoded[i + 2], 0 };
                    char byteVal = (char)wcstol(hex, nullptr, 16);
                    utf8Str.push_back(byteVal);
                    i += 2;
                } else {
                    utf8Str.push_back((char)encoded[i]);
                }
            }
            int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), (int)utf8Str.size(), NULL, 0);
            if (sizeNeeded > 0) {
                std::wstring wide(sizeNeeded, 0);
                MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), (int)utf8Str.size(), &wide[0], sizeNeeded);
                return SanitizeFilename(wide);
            }
        }
    }

    size_t fnPos = cdHeader.find(L"filename=");
    if (fnPos != std::wstring::npos) {
        std::wstring sub = cdHeader.substr(fnPos + 9);
        std::wstring fname;
        if (!sub.empty() && sub[0] == L'"') {
            size_t endQ = sub.find(L'"', 1);
            if (endQ != std::wstring::npos) fname = sub.substr(1, endQ - 1);
        } else {
            size_t endS = sub.find(L';');
            if (endS != std::wstring::npos) sub = sub.substr(0, endS);
            fname = sub;
        }

        // If filename contains % (URL percent-encoded UTF-8), decode it
        if (fname.find(L'%') != std::wstring::npos) {
            std::string utf8Str;
            for (size_t i = 0; i < fname.length(); ++i) {
                if (fname[i] == L'%' && i + 2 < fname.length()) {
                    wchar_t hex[3] = { fname[i + 1], fname[i + 2], 0 };
                    char byteVal = (char)wcstol(hex, nullptr, 16);
                    utf8Str.push_back(byteVal);
                    i += 2;
                } else {
                    utf8Str.push_back((char)fname[i]);
                }
            }
            int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), (int)utf8Str.size(), NULL, 0);
            if (sizeNeeded > 0) {
                std::wstring wide(sizeNeeded, 0);
                MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), (int)utf8Str.size(), &wide[0], sizeNeeded);
                return SanitizeFilename(wide);
            }
        }

        return SanitizeFilename(fname);
    }
    return L"";
}

bool HttpEngine::QueryUrlInfo(
    const std::wstring& url,
    const std::wstring& referer,
    const std::wstring& cookies,
    HttpHeadInfo& outInfo
) {
    outInfo.finalUrl = url;
    outInfo.statusCode = 0;
    outInfo.contentLength = -1;
    outInfo.acceptRanges = false;

    URL_COMPONENTS urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwExtraInfoLength = (DWORD)-1;

    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.length(), 0, &urlComp)) {
        return false;
    }

    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path = (urlComp.lpszUrlPath && urlComp.dwUrlPathLength > 0)
        ? std::wstring(urlComp.lpszUrlPath, urlComp.dwUrlPathLength)
        : L"/";
    if (urlComp.lpszExtraInfo && urlComp.dwExtraInfoLength > 0) {
        path.append(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
    }

    HINTERNET hSession = WinHttpOpen(
        DEFAULT_USER_AGENT,
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!hSession) return false;
    WinHttpSetTimeouts(hSession, 5000, 10000, 10000, 15000);

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::wstring effectiveReferer = referer;
    if (effectiveReferer.empty()) {
        if (url.find(L"surrit.com") != std::wstring::npos) {
            effectiveReferer = L"https://missav123.com/";
        }
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    // We send a GET request with Range: bytes=0-0 to test Range support and get accurate Content-Range and Content-Length
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        path.c_str(),
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags
    );

    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // Enable HTTP/2 protocol flag
    DWORD protocols = WINHTTP_PROTOCOL_FLAG_HTTP2;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &protocols, sizeof(protocols));

    // Ignore SSL certificate errors if needed
    if (flags & WINHTTP_FLAG_SECURE) {
        DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                         SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    }

    // Enable automatic redirect following across domains/protocols
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    // Disable auto-decompression so Content-Length matches the actual raw byte count
    DWORD autoDecomp = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_DECOMPRESSION, &autoDecomp, sizeof(autoDecomp));

    std::wstring headers = L"Range: bytes=0-0\r\n";
    if (!cookies.empty()) {
        headers += L"Cookie: " + cookies + L"\r\n";
    }
    if (!effectiveReferer.empty()) {
        headers += L"Referer: " + effectiveReferer + L"\r\n";
    }

    BOOL bSend = WinHttpSendRequest(
        hRequest,
        headers.c_str(),
        (DWORD)headers.length(),
        NULL,
        0,
        0,
        0
    );

    if (bSend && WinHttpReceiveResponse(hRequest, NULL)) {
        DWORD statusCode = 0;
        DWORD dwSize = sizeof(statusCode);
        WinHttpQueryHeaders(
            hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &dwSize,
            WINHTTP_NO_HEADER_INDEX
        );
        outInfo.statusCode = statusCode;

        // Query final redirected URL if available
        DWORD finalUrlLen = 0;
        WinHttpQueryOption(hRequest, WINHTTP_OPTION_URL, NULL, &finalUrlLen);
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && finalUrlLen > 0) {
            std::vector<WCHAR> finalUrlBuf(finalUrlLen / sizeof(WCHAR) + 1, 0);
            if (WinHttpQueryOption(hRequest, WINHTTP_OPTION_URL, finalUrlBuf.data(), &finalUrlLen)) {
                outInfo.finalUrl = finalUrlBuf.data();
            }
        }

        // Check if server supports Range (206 Partial Content)
        if (statusCode == 206) {
            outInfo.acceptRanges = true;
            // Parse Content-Range: bytes 0-0/123456
            WCHAR crBuf[256] = { 0 };
            DWORD crSize = sizeof(crBuf);
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_RANGE, WINHTTP_HEADER_NAME_BY_INDEX, crBuf, &crSize, WINHTTP_NO_HEADER_INDEX)) {
                std::wstring crStr = crBuf;
                size_t slash = crStr.find(L'/');
                if (slash != std::wstring::npos) {
                    try {
                        outInfo.contentLength = std::stoll(crStr.substr(slash + 1));
                    } catch (...) {}
                }
            }
        } else {
            // Check Accept-Ranges header
            WCHAR arBuf[128] = { 0 };
            DWORD arSize = sizeof(arBuf);
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_ACCEPT_RANGES, WINHTTP_HEADER_NAME_BY_INDEX, arBuf, &arSize, WINHTTP_NO_HEADER_INDEX)) {
                if (_wcsicmp(arBuf, L"bytes") == 0) {
                    outInfo.acceptRanges = true;
                }
            }
            // Normal Content-Length
            WCHAR clBuf[64] = { 0 };
            DWORD clSize = sizeof(clBuf);
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, clBuf, &clSize, WINHTTP_NO_HEADER_INDEX)) {
                try {
                    outInfo.contentLength = std::stoll(clBuf);
                } catch (...) {}
            }
        }

        // Content-Type
        WCHAR ctBuf[256] = { 0 };
        DWORD ctSize = sizeof(ctBuf);
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, ctBuf, &ctSize, WINHTTP_NO_HEADER_INDEX)) {
            outInfo.contentType = ctBuf;
        }

        // Content-Disposition
        WCHAR cdBuf[512] = { 0 };
        DWORD cdSize = sizeof(cdBuf);
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_DISPOSITION, WINHTTP_HEADER_NAME_BY_INDEX, cdBuf, &cdSize, WINHTTP_NO_HEADER_INDEX)) {
            outInfo.filename = ExtractFilenameFromContentDisposition(cdBuf);
        }

        if (outInfo.filename.empty() || IsGenericFilename(outInfo.filename)) {
            std::wstring fnFromFinal = ExtractFilenameFromUrl(outInfo.finalUrl);
            if (!fnFromFinal.empty() && !IsGenericFilename(fnFromFinal)) {
                outInfo.filename = fnFromFinal;
            } else if (outInfo.filename.empty()) {
                outInfo.filename = ExtractFilenameFromUrl(url);
            }
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return outInfo.statusCode > 0;
}

bool HttpEngine::DownloadRange(
    const std::wstring& url,
    const std::wstring& referer,
    const std::wstring& cookies,
    int64_t startByte,
    int64_t endByte,
    HANDLE hTargetFile,
    std::atomic<bool>& abortFlag,
    std::atomic<bool>& pauseFlag,
    std::function<void(int64_t bytesRead)> progressCallback,
    std::mutex* pFileMutex
) {
    URL_COMPONENTS urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwExtraInfoLength = (DWORD)-1;

    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.length(), 0, &urlComp)) {
        return false;
    }

    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path = (urlComp.lpszUrlPath && urlComp.dwUrlPathLength > 0)
        ? std::wstring(urlComp.lpszUrlPath, urlComp.dwUrlPathLength)
        : L"/";
    if (urlComp.lpszExtraInfo && urlComp.dwExtraInfoLength > 0) {
        path.append(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
    }

    HINTERNET hSession = WinHttpOpen(
        DEFAULT_USER_AGENT,
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!hSession) return false;
    WinHttpSetTimeouts(hSession, 5000, 10000, 10000, 15000);

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::wstring effectiveReferer = referer;
    if (effectiveReferer.empty()) {
        if (url.find(L"surrit.com") != std::wstring::npos) {
            effectiveReferer = L"https://missav123.com/";
        }
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        path.c_str(),
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags
    );

    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // Enable HTTP/2 protocol flag
    DWORD protocols = WINHTTP_PROTOCOL_FLAG_HTTP2;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &protocols, sizeof(protocols));

    if (flags & WINHTTP_FLAG_SECURE) {
        DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                         SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    }

    // Enable automatic redirect following across domains/protocols
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    std::wstring headers;
    if (startByte >= 0 && endByte >= startByte) {
        headers = L"Range: bytes=" + std::to_wstring(startByte) + L"-" + std::to_wstring(endByte) + L"\r\n";
    }
    if (!cookies.empty()) {
        headers += L"Cookie: " + cookies + L"\r\n";
    }
    if (!effectiveReferer.empty()) {
        headers += L"Referer: " + effectiveReferer + L"\r\n";
    }

    BOOL bSend = WinHttpSendRequest(
        hRequest,
        headers.c_str(),
        (DWORD)headers.length(),
        NULL,
        0,
        0,
        0
    );

    bool success = false;
    if (bSend && WinHttpReceiveResponse(hRequest, NULL)) {
        DWORD statusCode = 0;
        DWORD dwSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);

        if (statusCode == 200 || statusCode == 206) {
            std::vector<char> buffer(64 * 1024);
            int64_t currentFileOffset = startByte >= 0 ? startByte : 0;

            while (!abortFlag && !pauseFlag) {
                DWORD bytesToRead = (DWORD)buffer.size();
                if (endByte >= startByte) {
                    int64_t remaining = (endByte - currentFileOffset) + 1;
                    if (remaining <= 0) {
                        success = true;
                        break;
                    }
                    if ((int64_t)bytesToRead > remaining) {
                        bytesToRead = (DWORD)remaining;
                    }
                }

                DWORD bytesRead = 0;
                if (!WinHttpReadData(hRequest, buffer.data(), bytesToRead, &bytesRead) || bytesRead == 0) {
                    // EOF reached
                    success = (endByte < startByte) || (currentFileOffset > endByte);
                    break;
                }

                OVERLAPPED ov = { 0 };
                ov.Offset = (DWORD)(currentFileOffset & 0xFFFFFFFF);
                ov.OffsetHigh = (DWORD)((currentFileOffset >> 32) & 0xFFFFFFFF);

                DWORD bytesWritten = 0;
                BOOL writeOk = FALSE;
                if (pFileMutex) {
                    std::lock_guard<std::mutex> fLock(*pFileMutex);
                    writeOk = WriteFile(hTargetFile, buffer.data(), bytesRead, &bytesWritten, &ov);
                } else {
                    writeOk = WriteFile(hTargetFile, buffer.data(), bytesRead, &bytesWritten, &ov);
                }

                if (!writeOk) {
                    success = false;
                    break;
                }

                currentFileOffset += bytesRead;
                if (progressCallback) {
                    progressCallback(bytesRead);
                }

                if (endByte >= startByte && currentFileOffset > endByte) {
                    success = true;
                    break;
                }
            }
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return success;
}

bool HttpEngine::DownloadBuffer(
    const std::wstring& url,
    const std::wstring& referer,
    const std::wstring& cookies,
    std::string& outData,
    std::atomic<bool>& abortFlag
) {
    outData.clear();
    URL_COMPONENTS urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwExtraInfoLength = (DWORD)-1;

    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.length(), 0, &urlComp)) {
        return false;
    }

    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path = (urlComp.lpszUrlPath && urlComp.dwUrlPathLength > 0)
        ? std::wstring(urlComp.lpszUrlPath, urlComp.dwUrlPathLength)
        : L"/";
    if (urlComp.lpszExtraInfo && urlComp.dwExtraInfoLength > 0) {
        path.append(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
    }

    HINTERNET hSession = WinHttpOpen(
        DEFAULT_USER_AGENT,
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!hSession) return false;
    WinHttpSetTimeouts(hSession, 5000, 10000, 10000, 15000);

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::wstring effectiveReferer = referer;
    if (effectiveReferer.empty()) {
        if (url.find(L"surrit.com") != std::wstring::npos) {
            effectiveReferer = L"https://missav123.com/";
        }
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        path.c_str(),
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags
    );

    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // Enable HTTP/2 protocol flag
    DWORD protocols = WINHTTP_PROTOCOL_FLAG_HTTP2;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &protocols, sizeof(protocols));

    if (flags & WINHTTP_FLAG_SECURE) {
        DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                         SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    }

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    std::wstring headers;
    if (!cookies.empty()) {
        headers += L"Cookie: " + cookies + L"\r\n";
    }
    if (!effectiveReferer.empty()) {
        headers += L"Referer: " + effectiveReferer + L"\r\n";
    }

    BOOL bSend = WinHttpSendRequest(
        hRequest,
        headers.c_str(),
        (DWORD)headers.length(),
        NULL,
        0,
        0,
        0
    );

    bool success = false;
    if (bSend && WinHttpReceiveResponse(hRequest, NULL)) {
        DWORD statusCode = 0;
        DWORD dwSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);

        if (statusCode == 200) {
            std::vector<char> buffer(16 * 1024);
            DWORD bytesRead = 0;
            while (!abortFlag && WinHttpReadData(hRequest, buffer.data(), (DWORD)buffer.size(), &bytesRead) && bytesRead > 0) {
                outData.append(buffer.data(), bytesRead);
            }
            success = !abortFlag;
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return success;
}

} // namespace EasyDownload
