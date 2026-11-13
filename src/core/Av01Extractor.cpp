#include "Av01Extractor.h"
#include "HttpEngine.h"
#include <vector>
#include <sstream>
#include <algorithm>
#include <regex>

namespace EasyDownload {

static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), NULL, 0);
    if (size <= 0) return L"";
    std::wstring out(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &out[0], size);
    return out;
}

static std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), NULL, 0, NULL, NULL);
    if (size <= 0) return "";
    std::string out(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &out[0], size, NULL, NULL);
    return out;
}

static std::wstring ExtractJsonString(const std::string& json, const std::string& key, size_t startOffset = 0) {
    std::string needle = "\"" + key + "\"";
    size_t kPos = json.find(needle, startOffset);
    if (kPos == std::string::npos) return L"";

    size_t colon = json.find(':', kPos + needle.length());
    if (colon == std::string::npos) return L"";

    size_t quoteStart = json.find('\"', colon + 1);
    if (quoteStart == std::string::npos) return L"";

    std::wstring result;
    std::string utf8Chunk;
    bool escaped = false;

    auto flushUtf8 = [&]() {
        if (!utf8Chunk.empty()) {
            result += Utf8ToWide(utf8Chunk);
            utf8Chunk.clear();
        }
    };

    for (size_t i = quoteStart + 1; i < json.length(); ++i) {
        char c = json[i];
        if (escaped) {
            if (c == 'n') { utf8Chunk += '\n'; }
            else if (c == 'r') { utf8Chunk += '\r'; }
            else if (c == 't') { utf8Chunk += '\t'; }
            else if (c == '\"') { utf8Chunk += '\"'; }
            else if (c == '\\') { utf8Chunk += '\\'; }
            else if (c == 'u' && i + 4 < json.length()) {
                flushUtf8();
                std::string hexStr = json.substr(i + 1, 4);
                try {
                    wchar_t codePoint = (wchar_t)std::stoul(hexStr, nullptr, 16);
                    result.push_back(codePoint);
                    i += 4;
                } catch (...) {
                    utf8Chunk += 'u';
                }
            } else {
                utf8Chunk += c;
            }
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '\"') {
            break;
        } else {
            utf8Chunk += c;
        }
    }
    flushUtf8();
    return result;
}

static std::string ExtractJsonRaw(const std::string& json, const std::string& key, size_t startOffset = 0) {
    std::string needle = "\"" + key + "\"";
    size_t kPos = json.find(needle, startOffset);
    if (kPos == std::string::npos) return "";

    size_t colon = json.find(':', kPos + needle.length());
    if (colon == std::string::npos) return "";

    size_t vStart = json.find_first_not_of(" \t\r\n", colon + 1);
    if (vStart == std::string::npos) return "";

    if (json[vStart] == '\"') {
        size_t vEnd = json.find('\"', vStart + 1);
        if (vEnd == std::string::npos) return "";
        return json.substr(vStart + 1, vEnd - vStart - 1);
    } else {
        size_t vEnd = json.find_first_of(",}\r\n \t", vStart);
        if (vEnd == std::string::npos) vEnd = json.length();
        return json.substr(vStart, vEnd - vStart);
    }
}

bool Av01Extractor::IsSupported(const std::wstring& url) {
    return (url.find(L"av01.media") != std::wstring::npos ||
            url.find(L"av01.tv") != std::wstring::npos ||
            url.find(L"iw01.xyz") != std::wstring::npos);
}

std::wstring Av01Extractor::ExtractVideoId(const std::wstring& url) {
    std::wstring videoId;
    size_t vPos = url.find(L"/video/");
    if (vPos != std::wstring::npos) {
        vPos += 7;
    } else {
        vPos = url.find(L"/videos/");
        if (vPos != std::wstring::npos) {
            vPos += 8;
        }
    }
    if (vPos != std::wstring::npos) {
        size_t endPos = url.find_first_not_of(L"0123456789", vPos);
        videoId = url.substr(vPos, (endPos != std::wstring::npos ? endPos - vPos : std::wstring::npos));
    }
    return videoId;
}

bool Av01Extractor::FetchFreshAccessToken(
    const std::wstring& videoId,
    std::wstring& outToken,
    std::atomic<bool>& abortFlag
) {
    outToken.clear();
    std::wstring defaultReferer = L"https://www.av01.media/";

    // 1. Fetch Geo Token from files.iw01.xyz
    std::wstring geoUrl = L"https://files.iw01.xyz/edge/geo.js?json";
    std::string geoJson;
    if (!HttpEngine::DownloadBuffer(geoUrl, defaultReferer, L"", geoJson, abortFlag)) {
        return false;
    }

    std::string tokenV2 = ExtractJsonRaw(geoJson, "token_v2");
    std::string expires = ExtractJsonRaw(geoJson, "expires");
    std::string ip = ExtractJsonRaw(geoJson, "ip");
    if (tokenV2.empty() || expires.empty() || ip.empty()) {
        return false;
    }

    if (abortFlag) return false;

    // 2. Fetch CDN Access Token
    std::string cdnAccessUrl = "https://customers.iw01.xyz/api/v1/videos/" + WideToUtf8(videoId) +
        "/cdn-access?token_v2=" + tokenV2 + "&expires=" + expires + "&ip=" + ip;
    std::wstring cdnAccessUrlW = Utf8ToWide(cdnAccessUrl);
    std::string cdnJson;
    if (!HttpEngine::DownloadBuffer(cdnAccessUrlW, defaultReferer, L"", cdnJson, abortFlag)) {
        return false;
    }

    std::string accessToken = ExtractJsonRaw(cdnJson, "access_token");
    if (accessToken.empty()) {
        return false;
    }

    outToken = Utf8ToWide(accessToken);
    return true;
}

bool Av01Extractor::ExtractStream(
    const std::wstring& pageUrl,
    std::wstring& outStreamUrl,
    std::wstring& outTitle,
    std::wstring& outReferer,
    std::atomic<bool>& abortFlag
) {
    outStreamUrl.clear();
    outTitle.clear();
    outReferer.clear();

    // 1. Extract Video ID: e.g. /video/191115
    std::wstring videoId = ExtractVideoId(pageUrl);
    if (videoId.empty()) {
        return false;
    }

    std::wstring defaultReferer = L"https://www.av01.media/";
    outReferer = defaultReferer;

    // 2. Fetch Video Metadata for title and dvd_id (optional, fallback if fails)
    std::wstring metaUrl = L"https://www.av01.media/api/v1/videos/" + videoId;
    std::string metaJson;
    if (HttpEngine::DownloadBuffer(metaUrl, defaultReferer, L"", metaJson, abortFlag)) {
        std::wstring dvdId = ExtractJsonString(metaJson, "dvd_id");
        std::wstring title;
        // Check for Korean translation in title_translations first
        size_t transPos = metaJson.find("\"title_translations\"");
        if (transPos != std::string::npos) {
            title = ExtractJsonString(metaJson, "ko", transPos);
        }
        if (title.empty()) {
            title = ExtractJsonString(metaJson, "title");
        }

        std::wstring combinedTitle;
        if (!dvdId.empty() && !title.empty()) {
            combinedTitle = L"[" + dvdId + L"] " + title;
        } else if (!dvdId.empty()) {
            combinedTitle = dvdId;
        } else if (!title.empty()) {
            combinedTitle = title;
        }

        if (!combinedTitle.empty()) {
            outTitle = HttpEngine::SanitizeFilename(combinedTitle, 80);
        }
    }

    if (outTitle.empty()) {
        outTitle = L"av01_" + videoId;
    }

    if (abortFlag) return false;

    // 3. Fetch Fresh CDN Access Token
    std::wstring accessToken;
    if (!FetchFreshAccessToken(videoId, accessToken, abortFlag) || accessToken.empty()) {
        return false;
    }

    if (abortFlag) return false;

    // 4. Fetch master manifest to find the highest resolution sub-variant
    std::wstring masterUrl = L"https://customers.iw01.xyz/api/v1/videos/" + videoId + L"/manifest/master.m3u8";
    std::string masterM3u8;
    std::string bestVariant = "index90-sv3-v1-a1.m3u8"; // default 1080p variant
    if (HttpEngine::DownloadBuffer(masterUrl, defaultReferer, L"", masterM3u8, abortFlag)) {
        std::istringstream stream(masterM3u8);
        std::string line;
        std::string lastVariant;
        while (std::getline(stream, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (!line.empty() && line[0] != '#') {
                lastVariant = line;
            }
        }
        if (!lastVariant.empty()) {
            size_t q = lastVariant.find('?');
            if (q != std::string::npos) lastVariant = lastVariant.substr(0, q);
            bestVariant = lastVariant;
        }
    }

    if (abortFlag) return false;

    // 5. Build final authenticated HLS playlist URL
    outStreamUrl = L"https://customers.iw01.xyz/api/v1/videos/" + videoId +
        L"/manifest/" + Utf8ToWide(bestVariant) + L"?access_token=" + accessToken;

    return true;
}

} // namespace EasyDownload

