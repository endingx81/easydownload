#include "LinkExtractor.h"
#include "HttpEngine.h"
#include <regex>
#include <sstream>
#include <winhttp.h>

namespace EasyDownload {

static const WCHAR* DEFAULT_USER_AGENT = L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";

static std::wstring UnescapeHtmlEntities(const std::wstring& str) {
    std::wstring s = str;
    auto replaceAll = [&](const std::wstring& from, const std::wstring& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::wstring::npos) {
            s.replace(pos, from.length(), to);
            pos += to.length();
        }
    };
    replaceAll(L"&#038;", L"&");
    replaceAll(L"&amp;", L"&");
    replaceAll(L"&#38;", L"&");
    replaceAll(L"&quot;", L"\"");
    replaceAll(L"&#39;", L"'");
    replaceAll(L"&apos;", L"'");
    replaceAll(L"&lt;", L"<");
    replaceAll(L"&gt;", L">");
    return s;
}

std::vector<ExtractedLink> LinkExtractor::ParseRawText(const std::wstring& rawText, const std::wstring& hostFilter) {
    std::vector<ExtractedLink> results;
    if (rawText.empty()) return results;

    // Split text into host blocks if labeled like "Send: ... 1Fichier: ... Megaup: ..."
    // Common host prefixes
    struct HostSection {
        size_t pos;
        std::wstring name;
    };
    std::vector<HostSection> sections;

    auto findSections = [&](const std::wstring& prefix, const std::wstring& canonicalName) {
        size_t pos = 0;
        while ((pos = rawText.find(prefix, pos)) != std::wstring::npos) {
            sections.push_back({ pos, canonicalName });
            pos += prefix.length();
        }
    };

    findSections(L"Send:", L"Send.now");
    findSections(L"Send.now:", L"Send.now");
    findSections(L"Sendcm:", L"Send.now");
    findSections(L"1Fichier:", L"1Fichier");
    findSections(L"1fichier:", L"1Fichier");
    findSections(L"Megaup:", L"Megaup");
    findSections(L"megaup:", L"Megaup");
    findSections(L"Gofile:", L"Gofile");
    findSections(L"PixelDrain:", L"PixelDrain");

    std::sort(sections.begin(), sections.end(), [](const HostSection& a, const HostSection& b) {
        return a.pos < b.pos;
    });

    auto getHostAtPos = [&](size_t pos) -> std::wstring {
        if (sections.empty()) return L"";
        std::wstring current = L"";
        for (const auto& sec : sections) {
            if (sec.pos <= pos) {
                current = sec.name;
            } else {
                break;
            }
        }
        return current;
    };

    // 1. Try Markdown links: [Label](URL) (Size)
    // Example: [Part.1](https://nswvault.com/.../1) (6 GB)
    std::wregex mdRegex(L"\\[([^\\]]+)\\]\\((https?://[^\\)\\s]+)\\)(?:\\s*\\(([0-9.]+\\s*[GMK]B)\\))?");
    auto mdBegin = std::wsregex_iterator(rawText.begin(), rawText.end(), mdRegex);
    auto mdEnd = std::wsregex_iterator();

    int partCounter = 1;
    for (auto it = mdBegin; it != mdEnd; ++it) {
        std::wsmatch match = *it;
        size_t matchPos = (size_t)match.position(0);
        std::wstring label = match[1].str();
        std::wstring url = UnescapeHtmlEntities(match[2].str());
        std::wstring size = match.size() > 3 && match[3].matched ? match[3].str() : L"";

        std::wstring host = getHostAtPos(matchPos);
        if (host.empty()) {
            if (url.find(L"send.now") != std::wstring::npos || url.find(L"send.cm") != std::wstring::npos) host = L"Send.now";
            else if (url.find(L"1fichier.com") != std::wstring::npos) host = L"1Fichier";
            else if (url.find(L"megaup.net") != std::wstring::npos) host = L"Megaup";
            else host = L"Direct";
        }

        // Extract part number from label (e.g. Part.1, Part 1, #1)
        int partIdx = partCounter++;
        std::wregex partNumRegex(L"(?:Part|part|[#])\\s*\\.?\\s*(\\d+)");
        std::wsmatch partMatch;
        if (std::regex_search(label, partMatch, partNumRegex)) {
            try {
                partIdx = std::stoi(partMatch[1].str());
            } catch (...) {}
        }

        ExtractedLink item;
        item.partIndex = partIdx;
        item.label = label;
        item.hostName = host;
        item.originalUrl = url;
        item.sizeStr = size;
        item.status = LinkStatus::Parsed;
        item.statusMessage = L"분석 대기";

        results.push_back(item);
    }

    // 2. If no markdown links were found, parse raw bare URLs line by line
    if (results.empty()) {
        std::wregex urlRegex(L"(https?://[^\\s<>'\"\\)]+)");
        auto urlBegin = std::wsregex_iterator(rawText.begin(), rawText.end(), urlRegex);
        auto urlEnd = std::wsregex_iterator();

        int counter = 1;
        for (auto it = urlBegin; it != urlEnd; ++it) {
            std::wsmatch match = *it;
            size_t matchPos = (size_t)match.position(0);
            std::wstring url = UnescapeHtmlEntities(match[1].str());

            std::wstring host = getHostAtPos(matchPos);
            if (host.empty()) {
                if (url.find(L"send.now") != std::wstring::npos || url.find(L"send.cm") != std::wstring::npos) host = L"Send.now";
                else if (url.find(L"1fichier.com") != std::wstring::npos) host = L"1Fichier";
                else if (url.find(L"megaup.net") != std::wstring::npos) host = L"Megaup";
                else host = L"Direct";
            }

            ExtractedLink item;
            item.partIndex = counter;
            item.label = L"Part " + std::to_wstring(counter);
            item.hostName = host;
            item.originalUrl = url;
            item.status = LinkStatus::Parsed;
            item.statusMessage = L"분석 대기";

            results.push_back(item);
            counter++;
        }
    }

    // Apply host filter if specified
    if (!hostFilter.empty() && hostFilter != L"All" && hostFilter != L"모든 호스트") {
        std::vector<ExtractedLink> filtered;
        for (const auto& item : results) {
            if (_wcsicmp(item.hostName.c_str(), hostFilter.c_str()) == 0 ||
                item.hostName.find(hostFilter) != std::wstring::npos) {
                filtered.push_back(item);
            }
        }
        if (!filtered.empty()) {
            return filtered;
        }
    }

    return results;
}

bool LinkExtractor::ResolveNswvault(
    const std::wstring& nswUrl,
    std::wstring& outTargetUrl,
    std::wstring& outSize,
    std::atomic<bool>& abortFlag
) {
    std::string html;
    if (!HttpEngine::DownloadBuffer(nswUrl, L"https://nswvault.com/", L"", html, abortFlag)) {
        return false;
    }

    // Extract <a id="dl-link" ... href="([^"]+)"
    std::regex linkRegex(R"(id=["']dl-link["'][^>]*href=["']([^"']+)["'])");
    std::smatch m;
    if (std::regex_search(html, m, linkRegex)) {
        std::string rawUrl = m[1].str();
        int sz = MultiByteToWideChar(CP_UTF8, 0, rawUrl.data(), (int)rawUrl.size(), NULL, 0);
        if (sz > 0) {
            std::wstring wUrl(sz, 0);
            MultiByteToWideChar(CP_UTF8, 0, rawUrl.data(), (int)rawUrl.size(), &wUrl[0], sz);
            outTargetUrl = UnescapeHtmlEntities(wUrl);
        }
    } else {
        // Fallback: check href=["']([^"']+)["'][^>]*id=["']dl-link["']
        std::regex altRegex(R"(href=["']([^"']+)["'][^>]*id=["']dl-link["'])");
        if (std::regex_search(html, m, altRegex)) {
            std::string rawUrl = m[1].str();
            int sz = MultiByteToWideChar(CP_UTF8, 0, rawUrl.data(), (int)rawUrl.size(), NULL, 0);
            if (sz > 0) {
                std::wstring wUrl(sz, 0);
                MultiByteToWideChar(CP_UTF8, 0, rawUrl.data(), (int)rawUrl.size(), &wUrl[0], sz);
                outTargetUrl = UnescapeHtmlEntities(wUrl);
            }
        }
    }

    // Extract size: e.g. <span class="rounded-full bg-brand-surface border border-brand-line px-2.5 py-1 text-xs font-semibold text-brand-text">\s*6 GB\s*</span>
    std::regex sizeRegex(R"(<span[^>]*class="[^"]*rounded-full[^"]*"[^>]*>\s*([0-9.]+\s*[GMK]B)\s*</span>)");
    if (std::regex_search(html, m, sizeRegex)) {
        std::string rawSize = m[1].str();
        int sz = MultiByteToWideChar(CP_UTF8, 0, rawSize.data(), (int)rawSize.size(), NULL, 0);
        if (sz > 0) {
            outSize.assign(sz, 0);
            MultiByteToWideChar(CP_UTF8, 0, rawSize.data(), (int)rawSize.size(), &outSize[0], sz);
        }
    }

    return !outTargetUrl.empty();
}

bool LinkExtractor::ResolveSendNow(
    const std::wstring& sendUrl,
    std::wstring& outDirectUrl,
    std::wstring& outFilename,
    int64_t& outFileSize,
    std::atomic<bool>& abortFlag
) {
    std::string html;
    if (!HttpEngine::DownloadBuffer(sendUrl, L"https://nswvault.com/", L"", html, abortFlag)) {
        return false;
    }

    // 1. Extract form fields: rand, id
    std::regex randRegex(R"(name=["']rand["']\s+value=["']([^"']+)["'])");
    std::regex idRegex(R"(name=["']id["']\s+value=["']([^"']+)["'])");
    std::smatch mRand, mId;

    std::string randVal, idVal;
    if (std::regex_search(html, mRand, randRegex)) {
        randVal = mRand[1].str();
    }
    if (std::regex_search(html, mId, idRegex)) {
        idVal = mId[1].str();
    }

    if (idVal.empty()) {
        // Extract id from sendUrl path
        size_t slash = sendUrl.find_last_of(L'/');
        if (slash != std::wstring::npos) {
            std::wstring idW = sendUrl.substr(slash + 1);
            int sz = WideCharToMultiByte(CP_UTF8, 0, idW.data(), (int)idW.size(), NULL, 0, NULL, NULL);
            if (sz > 0) {
                idVal.assign(sz, 0);
                WideCharToMultiByte(CP_UTF8, 0, idW.data(), (int)idW.size(), &idVal[0], sz, NULL, NULL);
            }
        }
    }

    if (randVal.empty() || idVal.empty()) {
        return false;
    }

    // Extract filename from HTML: e.g. <title>([^-<]+) or Fire Emblem[^\n<]+
    std::regex fnameRegex(R"((Fire[^<"\r\n]+?\.(?:zip|rar|7z|nup|nsp|xci)\.\d{3}))", std::regex_constants::icase);
    std::smatch mName;
    if (std::regex_search(html, mName, fnameRegex)) {
        std::string rawName = mName[1].str();
        int sz = MultiByteToWideChar(CP_UTF8, 0, rawName.data(), (int)rawName.size(), NULL, 0);
        if (sz > 0) {
            outFilename.assign(sz, 0);
            MultiByteToWideChar(CP_UTF8, 0, rawName.data(), (int)rawName.size(), &outFilename[0], sz);
            outFilename = HttpEngine::SanitizeFilename(outFilename);
        }
    }

    // 2. Send POST with WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW to capture 302 Location
    URL_COMPONENTS urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwExtraInfoLength = (DWORD)-1;

    if (!WinHttpCrackUrl(sendUrl.c_str(), (DWORD)sendUrl.length(), 0, &urlComp)) {
        return false;
    }

    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path = (urlComp.lpszUrlPath && urlComp.dwUrlPathLength > 0)
        ? std::wstring(urlComp.lpszUrlPath, urlComp.dwUrlPathLength)
        : L"/";

    HINTERNET hSession = WinHttpOpen(
        DEFAULT_USER_AGENT,
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!hSession) return false;
    WinHttpSetTimeouts(hSession, 8000, 10000, 10000, 20000);

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"POST",
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

    // Explicitly disallow automatic redirects so we capture the 302 Found direct file Location
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    if (flags & WINHTTP_FLAG_SECURE) {
        DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                         SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    }

    std::wstring headers = L"Content-Type: application/x-www-form-urlencoded\r\nReferer: " + sendUrl + L"\r\n";
    std::string postData = "op=download2&id=" + idVal + "&rand=" + randVal + "&referer=https%3A%2F%2Fnswvault.com%2F&method_free=";

    BOOL bSend = WinHttpSendRequest(
        hRequest,
        headers.c_str(),
        (DWORD)headers.length(),
        (LPVOID)postData.data(),
        (DWORD)postData.size(),
        (DWORD)postData.size(),
        0
    );

    bool ok = false;
    if (bSend && WinHttpReceiveResponse(hRequest, NULL)) {
        DWORD statusCode = 0;
        DWORD dwSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);

        if (statusCode == 302 || statusCode == 301 || statusCode == 303 || statusCode == 200) {
            DWORD locSize = 0;
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, NULL, &locSize, WINHTTP_NO_HEADER_INDEX);
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && locSize > 0) {
                std::wstring location(locSize / sizeof(WCHAR), 0);
                if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, &location[0], &locSize, WINHTTP_NO_HEADER_INDEX)) {
                    while (!location.empty() && location.back() == L'\0') location.pop_back();
                    outDirectUrl = location;
                    ok = true;
                }
            }
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (ok && !outDirectUrl.empty()) {
        if (outFilename.empty()) {
            outFilename = HttpEngine::ExtractFilenameFromUrl(outDirectUrl);
        }
        return true;
    }

    return false;
}

bool LinkExtractor::ResolveLink(ExtractedLink& link, std::atomic<bool>& abortFlag) {
    link.status = LinkStatus::Resolving;
    link.statusMessage = L"분석 중...";

    std::wstring targetUrl = link.originalUrl;

    // Step 1: Resolve nswvault redirect if applicable
    if (targetUrl.find(L"nswvault.com") != std::wstring::npos && targetUrl.find(L"/download/") != std::wstring::npos) {
        std::wstring intermediateUrl;
        std::wstring sizeFromPage;
        if (ResolveNswvault(targetUrl, intermediateUrl, sizeFromPage, abortFlag)) {
            targetUrl = intermediateUrl;
            if (link.sizeStr.empty() && !sizeFromPage.empty()) {
                link.sizeStr = sizeFromPage;
            }
        } else {
            link.status = LinkStatus::Failed;
            link.statusMessage = L"nswvault 다운로드 링크 추출 실패";
            return false;
        }
    }

    // Determine host from targetUrl
    if (targetUrl.find(L"send.now") != std::wstring::npos || targetUrl.find(L"send.cm") != std::wstring::npos) {
        link.hostName = L"Send.now";
    } else if (targetUrl.find(L"1fichier.com") != std::wstring::npos) {
        link.hostName = L"1Fichier";
    } else if (targetUrl.find(L"megaup.net") != std::wstring::npos) {
        link.hostName = L"Megaup";
    }

    // Step 2: Resolve send.now to direct high-speed CDN URL
    if (link.hostName == L"Send.now") {
        std::wstring directUrl;
        std::wstring filename;
        int64_t fileSize = 0;
        if (ResolveSendNow(targetUrl, directUrl, filename, fileSize, abortFlag)) {
            link.directUrl = directUrl;
            if (!filename.empty()) link.filename = filename;
            link.status = LinkStatus::Resolved;
            link.statusMessage = L"직링크 준비 완료 (초고속 CDN)";
            return true;
        } else {
            // Even if direct post resolution failed, we still have the valid send.now intermediate URL
            link.directUrl = targetUrl;
            link.filename = HttpEngine::ExtractFilenameFromUrl(targetUrl);
            link.status = LinkStatus::Resolved;
            link.statusMessage = L"Send.now 링크 준비 완료";
            return true;
        }
    }

    // Step 3: For other hosts (1Fichier, Megaup, Direct)
    link.directUrl = targetUrl;
    link.filename = HttpEngine::ExtractFilenameFromUrl(targetUrl);
    link.status = LinkStatus::Resolved;
    link.statusMessage = L"추출 완료";
    return true;
}

} // namespace EasyDownload
